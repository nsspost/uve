# Гипотезы по отладке UVC

Обновлено: 2026-04-30. Последний разобранный `UVC.pcapng`: устройство `1/26`, захват от 2026-04-30 `18:24:06`.

Цель файла: вести живой список проверенных гипотез, чтобы не ходить кругами. После каждого изменения прошивки и проверки pcap обновляем факты, вывод и статус гипотез.

## 2026-04-30: USBX + обновленный HAL, изображения нет

Свежий `UVC.pcapng` после обновления HAL/USBX:

- Устройство перечисляется и проходит `PROBE/COMMIT`.
- `SET_INTERFACE alt=1`: frame `2945`, время `1.537739 s`.
- Хост ставит ISO IN URB на endpoint `0x81`.
- Все completion от устройства на `0x81` имеют `Packet Data Length = 0`.
- Внутри URB `128` ISO descriptors, у каждого `ISO Data length = 0`, `USBD_STATUS_SUCCESS`.
- Ошибок уровня pcap в этих ISO URB нет, но полезных видеоданных на шине тоже нет.

Отладочные регистры в момент теста:

- USBX/DCD вызывает `HAL_PCD_EP_Transmit(0x81, ..., 512)`.
- Первые слова буфера корректные: `0xD8FF0102`, `0x1000E0FF`, то есть bytes `02 01 FF D8 FF E0 00 10`.
- HAL PCD видит endpoint как ISO IN, `maxpacket = 512`, `xfer_len = 512`.
- Значит, текущая проблема не в JPEG и не в формировании UVC payload: корректный первый MJPEG packet доходит до HAL.

Новая гипотеза:

- Сбой находится ниже USBX video class, в HAL/LL OTG HS ISO IN scheduling/FIFO.
- Подозрительная разница в HAL non-DMA ISO IN ветке: parity bit `SODDFRM/SD0PID_SEVNFRM` ставился после `EPENA`, тогда как в DMA ветке HAL parity ставится до включения endpoint. Если бит кадра выставлен слишком поздно, endpoint стабильно промахивается по ISO window, и хост видит пустые successful ISO URB.

Изменение для проверки:

- Убран устаревший `USBD_HAL_TRANSFER_ABORT_NOT_SUPPORTED`: в обновленном HAL есть `HAL_PCD_EP_Abort()`, USBX должен иметь возможность реально abort/reset endpoint.
- В `USB_EPStartXfer()` для non-DMA ISO IN порядок изменен на: program `DIEPTSIZ`, выбрать `SODDFRM/SEVNFRM`, затем `CNAK|EPENA`, затем запись FIFO.
- Добавлена диагностика `usb_ll_iso_*` и `usb_ll_writepacket_*`, чтобы увидеть:
  - `usb_ll_iso_dieptsiz_after_program_dbg`;
  - `usb_ll_iso_diepctl_after_parity_dbg`;
  - `usb_ll_iso_diepctl_after_enable_dbg`;
  - `usb_ll_iso_dtxfsts_before_write_dbg / after_write_dbg`;
  - `usb_ll_iso_dieptsiz_before_write_dbg / after_write_dbg`;
  - `usb_ll_writepacket_first_word_dbg`.

## 2026-04-29: USBX standalone, поток замирает через несколько секунд

Свежий `UVC.pcapng` после перехода на USBX standalone:

- Устройство в захвате: USB address `13`, VID/PID `0483:5750`.
- Ненулевые UVC ISO IN payload на EP `0x81` идут только примерно с `6 s` по `11 s`.
- По секундам для `usb.device_address == 13`, `ep81`, device-to-host:
  - `6 s`: `12` ненулевых packet completion, `29097` байт;
  - `7 s`: `16`, `49805` байт;
  - `8 s`: `14`, `42285` байт;
  - `9 s`: `14`, `45138` байт;
  - `10 s`: `16`, `51695` байт;
  - `11 s`: `13`, `30123` байт;
  - `12..16 s`: ненулевых payload `0`, дальше идут только пустые/ошибочные ISO completion.
- После деградации хост получает ISO URB с нулевой длиной и `XACT/ISOCH_REQUEST_FAILED`, затем пытается abort/reset pipe и увести streaming interface в `alt=0`.

Вывод:

- Это не похоже на первичную порчу JPEG: до срыва данные реально идут, после срыва payload вообще перестает попадать на шину.
- В USBX standalone найден риск высыхания очереди: `USBD_VIDEO_StreamPayloadDone()` пополнял очередь только при `length != 0`, а USBX write task при пустой очереди может начать гонять zero-length transfer. После первого zero-length completion приложение уже не ставило новый payload.
- Второй риск: STM-пример `video_write_payload()` использовал `ux_utility_delay_ms()` между кадрами. В standalone это busy-wait внутри USBX processing path, то есть на время задержки сама USBX state machine не обслуживается.

Изменение для следующей проверки:

- `StreamPayloadDone` теперь пополняет очередь и после `length == 0`, если stream не остановлен.
- На `SET_INTERFACE alt=1` очередь payload предварительно заполняется до `ux_device_class_video_transmission_start()`.
- Блокирующая `ux_utility_delay_ms()` убрана из `video_write_payload()`. Пауза между кадрами заменена на неблокирующие header-only packets до следующего `next_frame_tick`.
- Добавлены debug counters `usbx_video_*`: смотреть прежде всего `usbx_video_payload_zero_dbg`, `usbx_video_payload_get_fail_dbg`, `usbx_video_payload_commit_fail_dbg`, `usbx_video_start_status_dbg`, `usbx_video_idle_header_dbg`, `usbx_video_frame_start_dbg`, `usbx_video_frame_done_dbg`.

HAL обновлять сейчас не первая гипотеза: pcap указывает на starvation payload queue на уровне USBX video app. HAL/USBX DCD стоит трогать отдельной веткой только если после этой правки очередь не высыхает, но ISO transfer все равно умирает ниже класса.

Результат проверки:

- После USBX queue/prefill/no-delay правки поток все равно повис после первого кадра.
- Значит, это изменение не считается решением и код USBX video app возвращен к состоянию `f98cf06`.
- Свежий pcap после отката: устройство `24`, EP `0x81`; есть несколько payload в `1..2 s`, затем с `2.448 s` начинаются `USBD_STATUS_ISOCH_REQUEST_FAILED / XACT_ERROR`, дальше поток пустой. Симптом по сути тот же.

Следующий эксперимент:

- Сделать USB HAL/LL строго как в STM webcam reference. Сравнение показало, что заголовки, `stm32h7xx_hal_pcd_ex.c` и USB HAL уже совпадали; отличались только наши `#pragma GCC optimize("O2")` в:
  - `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c`;
  - `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c`.
- Эти два файла заменены копиями из `.stm_webcam_ref`, теперь USB HAL/LL побайтно совпадает с STM reference для PCD/LL USB.
- Цель проверки: понять, не вносил ли `O2` на USB низком уровне ошибку timing/state machine.

## Текущие факты

- Устройство перечисляется как `0483:5750`.
- Текущий USB-путь только UVC; исходники и класс CDC убраны из сборки.
- USB Device Core и USB HAL/LL для STM32H7 заменены на версии из `x-cube-webcam`.
- Кэш не включаем без отдельного решения.
- USB DMA сейчас выключен.
- Тестовый поток чередует два встроенных JPEG:
  - `jpeg_frame_0`: `4289` байт.
  - `jpeg_frame_1`: `3085` байт.
- Текущий эксперимент:
  - `uvc_min_source_enable = 1`;
  - `UVC_IN_PACKET = 512`;
  - `uvc_ram_packet_enable = 1`;
  - `uvc_stm_iso_replay_enable = 0`;
  - `uvc_payload_packet_interval_ms = 0`;
  - producer тестовых JPEG по умолчанию согласован с UVC frame interval: `camera_pipeline_period_ms = UVC_FRAME_INTERVAL_MS`.
- Последний pcap после `IISOIXFR` remap/drop: видеопоток идет на `1.45.1`, длительность около `9.06 s`:
  - ISO URB от устройства: `566` с `iso_error_count = 0`;
  - `USBD_STATUS_XACT_ERROR = 0xc0000011`: `0`;
  - zero-length/error ISO descriptors в видеопотоке: `0`;
  - восстановлено MJPEG-кадров: `47`;
  - валидные JPEG `FF D8 ... FF D9`: `47`;
  - битые JPEG: `0`;
  - незавершенный хвост кадра: `0` байт;
  - размер кадров: `3085..4289` байт, средний около `3674` байт.
- Практический прогон после этой сборки:
  - поток стабилен на длительном запуске;
  - вынимание/вставка USB-кабеля не ломает перечисление и повторный старт;
  - визуальное качество лучше, чем на предыдущих сборках;
  - это состояние считать опорной точкой перед возвратом памяти, дисплея, JPEG-кодирования и увеличением кадра.
- До исправления в pcap видеопоток шел на `1.44.1`, EP `0x81`, HS ISO IN:
  - `UVC_IN_PACKET = 512` реально виден на шине: полный UVC packet `512` байт, payload после 2-байтного UVC header `510` байт;
  - восстановлено JPEG-кадров: `29`;
  - полные совпадения: `jpeg_frame_1=12`, `jpeg_frame_0=0`;
  - битые кадры: `17` (`jpeg_frame_0=14`, `jpeg_frame_1=3`);
  - все битые кадры отличаются отсутствием одного или двух целых payload chunks, а не случайной порчей байтов;
  - `jpeg_frame_1`: 3 раза потерян chunk index `3`, offset `1530`;
  - `jpeg_frame_0`: 12 раз потерян chunk index `5`, offset `2550`; 2 раза потеряны chunks `5` и `7`, offsets `2550` и `3570`;
  - USBPcap показывает `USBD_STATUS_XACT_ERROR = 0xc0000011` на ISO descriptors с нулевой длиной: `19` событий;
  - URB error count: `316` URB без ошибок, `13` URB с одной ошибкой, `3` URB с двумя ошибками;
  - число `XACT_ERROR` внутри битого кадра точно совпадает с числом недостающих chunks.
- По времени старты кадров идут около заявленных `200 ms` (`192/208 ms` чередованием). Явной корреляции с FPS/bitrate из дескриптора пока не видно: сбой происходит внутри кадра на отдельных ISO-транзакциях.
- В pcap недостающих payload-байтов нет на шине. Это не просто отказ видеоприложения их показать.

## Рабочий вывод

После `IISOIXFR` remap/drop свежий pcap больше не показывает потерь ISO payload: `XACT_ERROR = 0`, восстановленные JPEG валидны `47/47`.

Рабочий вывод: критическая ошибка была в обработке `IISOIXFR`. STM HAL сообщает это global-событие как `epnum = 0`, а наш UVC class ждал `epnum = 1`, поэтому событие потери ISO IN EP1 фактически не попадало в правильный class path. После remap EP0 -> EP1 и resync odd/even frame bit поток стабилизировался.

## Известное хорошее состояние

Это состояние нужно сохранить как baseline перед расширением функциональности.

## 2026-04-28: live/display JPEG, поток замирает после целых кадров

Свежий `UVC.pcapng` после включения памяти/дисплея/JPEG:

- UVC-адрес в захвате: `1/33`, endpoint `0x81`.
- `SET_INTERFACE alt=1`: около `1.403591 s`.
- До первого срыва восстановлено `86` валидных MJPEG-кадров.
- Размеры кадров: `1412` и `1422` байта.
- Первый сбой происходит не внутри JPEG, а после целого кадра:
  - последний валидный payload: `4.448785 s`;
  - следующий ISO event: `4.464615 s`, `data_len = 0`, `iso_error_count = 124`;
  - далее `4.480599 s`, `data_len = 0`, `iso_error_count = 128`, `usbd_status = 0xc0000b00`.
- После этого endpoint не восстанавливает нормальную передачу до `SET_INTERFACE alt=0`.

Вывод:

- Текущая проблема отличается от старой порчи JPEG: байты кадра не ломаются, поток теряет очередное ISO окно между кадрами.
- Маленькие JPEG `~1.4 KB` дают всего 3 UVC packets на кадр при `512` байтах MPS, поэтому между кадрами много пустых окон. Это хорошо объясняет, почему более крупные кадры раньше выглядели устойчивее.
- Текущий STM-like старт кадра был хрупким: из `SOF` отправлялся отдельный 2-байтный header-only packet, а первый настоящий MJPEG payload ставился только следующим `DataIn`. Если этот переход пропускает окно, Windows видит пустой ISO request и поток уходит в `0xc0000b00`.

Изменение для следующей проверки:

- В STM-like `SOF` path добавлен режим `uvc_stm_sof_direct_payload_enable = 1`: когда пора начинать/возобновлять кадр, `SOF` сразу вызывает отправку первого настоящего MJPEG payload через `UVC_STM_ExampleDataIn()`, без отдельного стартового header-only packet.
- Добавлены счетчики:
  - `uvc_stm_sof_direct_payload_calls`;
  - `uvc_stm_sof_direct_payload_ok`;
  - `uvc_stm_sof_direct_payload_fail`.
- В `USBD_UVC_IsoINIncomplete()` для случая “нет активного кадра” теперь сбрасывается ожидание frame interval и разрешается немедленный повтор последнего кадра на следующем `SOF`.
- Добавлен счетчик `uvc_class_iso_no_active_resync`.

Что смотреть после прошивки:

- Если гипотеза верна, в pcap после EOF не должно быть перехода в `data_len=0 / iso_error_count=128 / 0xc0000b00`.
- `uvc_stm_sof_direct_payload_ok` должен расти на стартах кадров.
- `uvc_stm_sof_direct_payload_fail` должен оставаться нулевым или редким; если растет, смотреть `uvc_ll_tx_last_status`, `DIEPCTL/DIEPTSIZ/DIEPINT`.
- Если поток все равно падает, но `direct_payload_fail = 0`, причина ниже уровня UVC class: TxFIFO/periodic scheduler/IISOIXFR recovery.

Результат проверки:

- После этой сборки пользователь сообщил, что устройство не определяется хостом.
- Хотя direct-payload path должен срабатывать только после `SET_INTERFACE alt=1`, эксперимент считается слишком рискованным для baseline.
- Откат для следующей сборки:
  - `uvc_stm_sof_direct_payload_enable = 0`;
  - `uvc_class_iso_no_active_resync_enable = 0`.
- Код и счетчики оставлены как выключаемый диагностический режим, чтобы при необходимости включать точечно из отладчика, а не менять поведение прошивки по умолчанию.

Что важно не потерять:

- USB class только UVC, без CDC.
- USB HS через ULPI.
- USB DMA выключен.
- Кэш не включен.
- ISO IN endpoint: `0x81`.
- HS packet size: `512`.
- Tx FIFO EP1: `0x300` words, как у STM x-cube-webcam.
- `DTHRCTL = 0x0C100020`.
- `uvc_stm_like_enable = 1`.
- `uvc_min_source_enable = 1`.
- `uvc_ram_packet_enable = 1`.
- `uvc_stm_iso_replay_enable = 0`.
- `uvc_payload_packet_interval_ms = 0`.
- `uvc_isoin_incomplete_handling_enable = 1` в текущей сборке: remap EP0 -> EP1 остается, ручной odd/even resync снова включен после неудачного эксперимента.
- `uvc_class_iso_drop_enable = 1`.

Причина проблемы:

- STM HAL для global-события `IISOIXFR` вызывает callback с `epnum = 0`.
- Реальная ошибка относилась к ISO IN EP1, но наш class callback фильтровал событие как будто оно должно прийти с `epnum = 1`.
- Из-за этого UVC class не синхронизировал EP1 после incomplete ISO transfer, поток продолжал идти в неверном состоянии и кадры/пакеты терялись.

Решение:

- В `PCD_ISOINIncompleteCallback()` remap-ить `epnum = 0` на `UVC_IN_EP & 0x7F`, то есть EP1.
- Для EP1 выполнять resync odd/even frame bit по `DSTS.FNSOF`.
- В `USBD_UVC_IsoINIncomplete()` сбрасывать текущий MJPEG frame state как защиту, а не пытаться replay.
- Replay оставить выключенным.

## Сверка с STM по FIFO и endpoint-буферам

Нижний уровень инициализации USB HS сейчас почти совпадает со STM:

| Параметр | У нас | STM x-cube-webcam | Комментарий |
| --- | --- | --- | --- |
| `dev_endpoints` | `4` | `4` | совпадает |
| `ep0_mps` | `0x40` | `0x40` | совпадает |
| DMA | `DISABLE` | `0` | совпадает; STM прямо предупреждает, что DMA может требовать кратности 4 |
| PHY | `ULPI` | `ULPI` | совпадает |
| SOF | включен | включен | совпадает |
| Rx FIFO | `0x64` words | `0x64` words | совпадает |
| Tx FIFO EP0 | `0x32` words | `0x32` words | совпадает |
| Tx FIFO EP1 | `0x300` words | `0x300` words | совпадает |
| `DTHRCTL` | `0x0C100020` | `0x0C100020` | совпадает |

Важное отличие до текущего эксперимента: raw FIFO-значения совпадали, но эффективная емкость в max-packets была разная:

- STM: `UVC_ISO_HS_MPS = 512`, Tx FIFO EP1 `0x300` words = `3072` байта, примерно 6 max-packets.
- У нас было: `UVC_IN_PACKET = 1024`, Tx FIFO EP1 `0x300` words = `3072` байта, примерно 3 max-packets.

В текущей проверке `UVC_IN_PACKET = 512`, поэтому FIFO EP1 должен снова давать примерно 6 max-packets, как у STM.

## Проверенные гипотезы

| Гипотеза | Статус | Доказательства / результат | Следующее действие |
| --- | --- | --- | --- |
| JPEG портится до USB | Почти исключено | Часть кадров `jpeg_frame_1` приходит байт-в-байт. Встроенные JPEG валидны: `FF D8 ... FF D9`. Битые кадры отличаются отсутствием ровных кусков, а не случайной порчей байтов. | Пока не трогать JPEG-кодек. |
| Кадры портит хост или видеоприложение | Почти исключено | USBPcap показывает, что недостающего payload нет в ISO data. Приложение не может показать байты, которые не пришли. | Можно потом проверить на Linux, но это не основной путь. |
| Не выставлялся EOF-бит UVC | Исправлено, не корень проблемы | Раньше было `EOFheaders=0`. После правки EOF есть на каждом кадре, но payload-потери остались. | Оставить EOF включенным. |
| Неверное число endpoint-ов или конфликт с CDC | Почти исключено | После удаления CDC устройство UVC перечисляется, поток стартует, ISO IN идет на EP `0x81`. | CDC не возвращать. `.ioc` может упоминать CDC, но он не источник истины. |
| USB core/HAL отличается от STM | Почти исключено для нижнего уровня | USB Device Core и USB HAL/LL заменены на STM reference. Проблема осталась. | Смотреть class/data-source logic. |
| FIFO/DTHRCTL отличаются от STM | Уточнено | Регистры FIFO/DTHRCTL совпадают. При `UVC_IN_PACKET = 512` эффективная емкость EP1 FIFO снова как у STM: примерно 6 max-packets. Потери остались. | Не главный подозреваемый, но оставить значения STM-like. |
| Дескрипторы fps/bitrate/bInterval заставляют хост дропать кадры | Сильно ослаблено | Старты кадров идут около `200 ms`, что соответствует `UVC_FRAME_INTERVAL_100NS = 2000000`. Ошибка возникает как `XACT_ERROR` внутри ISO-транзакции, а не как отказ хоста принимать кадр из-за темпа. | Пока не менять FPS. Вернуться только после RAM/direct experiment. |
| Размер пакета 512 вместо 1024 поможет | Не исправило полностью | `512` подтвержден в pcap, payload chunks стали по `510` байт. Потери остались, но теперь их легче читать и они точно совпадают с `XACT_ERROR`. | Оставить `512` для дальнейших сравнений. |
| Дисплей/LCD мешает USB | Почти исключено | Отключение дисплейной нагрузки не убрало проблему. В pcap видна конкретная потеря ISO payload. | Не считать главным подозреваемым. |
| Проблема в кэше | Не подтверждается | Кэш не включен. Пропадает целый payload, а не приходит stale data. | Кэш не включать. |
| Нужен USB DMA | Ослаблено | DMA on/off не дал ясного исправления. Сейчас DMA выключен, как у STM. | Не включать DMA вслепую. |
| IISO/ISO incomplete replay исправит потерю | Не исправило | `uvc_stm_iso_replay_enable=1` не убрал missing chunks. Возможно, incomplete callback приходит слишком поздно или replay хранит не тот state. | В текущей проверке replay выключен, чтобы убрать лишнюю ветку. |
| HAL сообщает `IISOIXFR` как EP0, поэтому UVC class не видит потерю EP1 | Подтверждено | В STM HAL global `IISOIXFR` callback приходит с `epnum = 0`, а приложение должно само сопоставить событие с ISO IN endpoint. Эксперимент с выключенным ручным resync дал резкое ухудшение: много `XACT_ERROR`, FID toggles/drop и почти нет EOF. | Оставить remap и ручной resync включенными. |
| При потере ISO packet нельзя продолжать текущий MJPEG-фрейм | Подтверждено как защитная логика | USBPcap до исправления показывал нулевой ISO descriptor/XACT_ERROR внутри кадра. Если после этого продолжать отправлять следующий chunk того же JPEG, хост получает кадр с дырой. | Оставить `uvc_class_iso_drop_enable = 1` как страховку. В нормальном pcap drop не должен часто срабатывать, потому что `XACT_ERROR` исчезли. |
| Пауза 1 ms между payload-пакетами исправит missed ISO slots | Не исправило | После `uvc_payload_packet_interval_ms = 1` статистика потерь та же. В USBPcap payload-пакеты все равно группируются внутри URB. | Отключено в текущей проверке: `uvc_payload_packet_interval_ms = 0`. |
| Активный путь уже точно STM-шный | Подтверждено для class/data-source | `UVC_STM_ExampleDataIn()` и минимальный `UVC_Itf_DataMinimal()` функционально повторяют STM-паттерн: следующий пакет готовится из `DataIn`, без pacing/replay. Проблема осталась. | Искать разницу ниже/вокруг USB timing или убрать лишнюю работу из `DataIn`. |
| Мы увеличиваем packet index раньше, чем пакет гарантированно ушел | Уже не выглядит корнем | В pcap missing chunk совпадает с `XACT_ERROR`: транзакция была запланирована, но пришла нулевой длины. Даже STM-like state machine после такого события естественно идет дальше. | Не строить новые replay-костыли, пока не найдена причина `XACT_ERROR`. |
| Не хватает времени/слота ISO из-за IRQ latency или чтения из flash | Ослаблено, если RAM/direct counters подтверждены | После RAM/direct pcap симптом не изменился: те же `XACT_ERROR` и те же missing chunks. Но нужно подтвердить в отладчике `uvc_ram_packet_ready_dbg=1` и рост `uvc_ram_packet_direct_tx`. | Если counters подтверждены, считать чтение из flash/memcpy не корнем. |
| Header-only packets между кадрами ломают поток | Ослаблено | В pcap много 2-байтных packets, но STM reference делает то же самое: при отсутствии кадра `VIDEO_Itf_Data()` возвращает `*psize = 2`. | Не менять до отдельного эксперимента. |
| ULPI/PHY/signal integrity или data-dependent HS ошибка | Открыта / усилилась | Потери повторяются на одних и тех же offsets внутри JPEG: `jpeg_frame_0` offset `2550`, `jpeg_frame_1` offset `1530`. Это может быть не темп, а конкретная USB-транзакция/битовый рисунок/физика HS. | Сравнить на другом кабеле/порту/хабе; затем проверить меньший `UVC_IN_PACKET` или другой JPEG-паттерн. |

## Важное состояние кода

- Активный путь стрима: `uvc_stm_like_enable = 1`.
- Активная class transmit-функция: `UVC_STM_ExampleDataIn()`.
- Активный источник пакетов: `UVC_Itf_DataMinimal()` при `uvc_min_source_enable = 1`.
- Старый источник пакетов остался как fallback: поставить `uvc_min_source_enable = 0`.
- `uvc_stm_header_eof_enable = 1`.
- `uvc_stm_iso_replay_enable = 0`.
- `uvc_isoin_incomplete_handling_enable = 1`: low-level callback remap-ит `IISOIXFR` EP0 на UVC EP1 и выставляет odd/even frame bit вручную.
- `uvc_class_iso_drop_enable = 1`: UVC class сбрасывает текущий MJPEG-фрейм при ISO incomplete, а не пытается replay.
- `uvc_payload_packet_interval_ms = 0`.
- `UVC_IN_PACKET = 512`, descriptor/probe/commit должны показывать `512`.
- `camera_pipeline_period_ms = UVC_FRAME_INTERVAL_MS`, чтобы producer тестовых кадров не шел быстрее объявленного frame interval.
- Новый эксперимент: `uvc_ram_packet_enable = 1`. Два тестовых JPEG заранее собираются в UVC-пакеты в RAM; STM-like `DataIn` отправляет prebuilt packet напрямую в `USBD_LL_Transmit`.

## Подтвержденное исправление: IISOIXFR remap/drop

Что изменено в прошивке:

- `PCD_ISOINIncompleteCallback()` теперь сохраняет raw endpoint отдельно: `pcd_isoin_incomplete_raw_ep`.
- Если HAL принес `epnum = 0` для global `IISOIXFR`, callback считает эффективным endpoint `UVC_IN_EP & 0x7F`, то есть EP1. Это видно по `pcd_isoin_incomplete_ep0_remaps` и `pcd_isoin_incomplete_effective_ep`.
- Для EP1 сохраняются `DSTS`, `DIEPCTL`, `DIEPTSIZ`, `DIEPINT`, offset и размер текущего кадра.
- При `uvc_isoin_incomplete_handling_enable = 1` обработчик заново выставляет odd/even ISO frame bit по `DSTS.FNSOF`.
- `USBD_UVC_IsoINIncomplete()` при `uvc_class_iso_drop_enable = 1` сбрасывает текущий frame state и помечает кадр как потерянный через `uvc_class_iso_frame_drops`.

Что смотреть в отладчике:

- `pcd_isoin_incomplete_raw_ep`: скорее всего будет `0`.
- `pcd_isoin_incomplete_effective_ep`: должен быть `1`.
- `pcd_isoin_incomplete_ep0_remaps`: должен расти вместе с ISO incomplete.
- `uvc_isoin_incomplete_resync_calls`: должен расти, если включен resync.
- `uvc_class_iso_frame_drops`: должен расти при реальных `XACT_ERROR`.
- `uvc_class_iso_drop_last_offset`, `uvc_class_iso_drop_last_frame_size`, `uvc_class_iso_drop_last_backend_packet_index`: где именно был сброшен кадр.

Результат проверки:

- На pcap от 2026-04-28 `07:40:15`: `XACT_ERROR = 0`.
- Восстановлено `47` MJPEG-кадров.
- Валидные JPEG: `47`.
- Битые JPEG: `0`.
- Незавершенный хвост кадра: `0` байт.
- Вывод: remap/resync надо считать основным исправлением, а frame drop оставить как защиту на случай редкой физической/host-side ISO ошибки.

## Следующие хорошие эксперименты

1. Длинный прогон и переподключение USB выполнены успешно:
   - поток стабилен;
   - устройство повторно перечисляется;
   - приложение снова видит камеру;
   - текущее состояние считать baseline.

2. Текущий следующий слой: память/SDRAM без дисплея и без JPEG encoder:
   - `main_sdram_enable = 1`;
   - `main_usb_only_mode = 1`;
   - вызываются только `MX_FMC_Init()` и `BSP_SDRAM_Init()`;
   - LTDC/SPI/DMA2D/JPEG остаются выключенными;
   - `sdram_smoke_test()` проверяет 256 слов в конце SDRAM около `0xD07FF000`;
   - ожидание в отладчике: `main_sdram_init_done = 1`, `main_sdram_test_done = 1`, `main_sdram_test_ok = 1`, `main_sdram_test_errors = 0`.

3. Возвращать функциональность строго по одному слою:
   - сначала память/буферы, без изменения UVC descriptors и размера кадра;
   - затем дисплей, без включения JPEG-кодирования в поток;
   - затем JPEG-кодирование на текущем маленьком размере кадра;
   - только после этого увеличивать размер кадра до размера дисплея.

4. После каждого слоя проверять:
   - камера видна после старта приложения;
   - поток идет без остановок;
   - pcap не содержит `XACT_ERROR`;
   - восстановленные JPEG имеют `FF D8 ... FF D9`;
   - `uvc_class_iso_frame_drops` не растет быстро.

5. При увеличении размера кадра:
   - сначала пересчитать `UVC_MAX_FRAME_SIZE`, `dwMaxVideoFrameSize`, bitrate и producer period;
   - оставить `UVC_IN_PACKET = 512`;
   - не включать USB DMA и кэш одновременно с ростом кадра;
   - убедиться, что JPEG buffer живет достаточно долго до завершения передачи кадра.

6. Не менять одновременно:
   - размер кадра;
   - источник данных;
   - дисплейную нагрузку;
   - JPEG encoder path;
   - cache/DMA/MPU;
   - USB descriptors/probe values.

## Правила чтения pcap

- Не делать выводы только по `usb.data_len`: USBPcap агрегирует много ISO descriptors в один URB.
- Восстанавливать кадры из `usb.iso.data`.
- У каждого ISO packet убирать 2-байтный UVC payload header.
- Сравнивать восстановленный JPEG с `jpeg_frame_0` и `jpeg_frame_1` байт-в-байт.
- Для старого `1024` MPS missing chunks считались по offsets `0`, `1022`, `2044`, `3066`, `4088`.
- Для текущего `512` MPS ожидаемые payload offsets идут с шагом `510`: `0`, `510`, `1020`, `1530`, `2040`, `2550`, ...

## Возврат периферии после победы над USB

### SDRAM

Статус: проверено успешно.

- `main_sdram_enable = 1`.
- `main_usb_only_mode = 1`.
- Инициализируются только `MX_FMC_Init()` и `BSP_SDRAM_Init()`.
- `sdram_smoke_test()` проходит: ожидается `main_sdram_test_ok = 1`, `main_sdram_test_errors = 0`.
- По результату прогона пользователя UVC-стрим остается стабильным.

Вывод: сама инициализация SDRAM и наличие `.xsdram` буферов не ломают USB baseline.

### Дисплей без JPEG encoder

Статус: проверено успешно.

- `main_display_enable = 1`.
- `main_usb_only_mode = 1` остается включенным.
- Включаются `MX_LTDC_Init()`, `MX_SPI5_Init()`, `ILI9488_Init()` и `MX_DMA2D_Init()`.
- Рисуется только стартовая статическая цветовая таблица в `fb`.
- UVC все еще отправляет встроенные тестовые JPEG, размер и дескрипторы не меняются.
- По результату прогона пользователя картинка на дисплее появилась, UVC-стрим живой и стабильный.

Вывод: LTDC/SPI/DMA2D и чтение framebuffer дисплеем из SDRAM не ломают USB baseline.

### JPEG/MDMA hardware init без live-кодирования

Статус: проверено успешно.

- `main_jpeg_hw_enable = 1`.
- `main_usb_only_mode = 1` остается включенным.
- Вызываются `MX_MDMA_Init()` и `MX_JPEG_Init()`.
- `camera_pipeline_force_test_jpeg = 1` остается защитным значением.
- UVC все еще отправляет встроенные тестовые JPEG, размер и дескрипторы не меняются.
- По результату прогона пользователя `main_mdma_init_done = 1`, `main_jpeg_init_done = 1`, дисплей и UVC работают визуально нормально.

Вывод: сам факт включения MDMA/JPEG peripheral не ломает USB baseline.

### Live JPEG на текущем UVC-размере 160x120

Статус: проверено успешно.

- `main_live_jpeg_enable = 1`.
- `main_usb_only_mode = 1` остается включенным.
- `camera_pipeline_force_test_jpeg = 0`, то есть источник UVC переключается со встроенных JPEG на live JPEG из framebuffer.
- Размеры live pipeline и JPEG encoder теперь берутся из `UVC_FRAME_WIDTH`/`UVC_FRAME_HEIGHT`.
- На этом этапе это `160x120`, как в текущих UVC descriptors/probe.
- Дисплей остается `320x480`, pipeline downsample-ит `fb` до `160x120`.
- `UVC_MAX_FRAME_SIZE = 8192` пока не увеличивается.

Ожидание в отладчике:

- `main_live_jpeg_init_done = 1`.
- `camera_pipeline_force_test_jpeg = 0`.
- `camera_pipeline_uvc_w_dbg = 160`.
- `camera_pipeline_uvc_h_dbg = 120`.
- `jpeg_live_src_w_dbg = 160`.
- `jpeg_live_src_h_dbg = 120`.
- `camera_pipeline_encode_ok` растет.
- `camera_pipeline_submit_ok` растет.
- `camera_pipeline_skip_oversize_jpeg = 0`.
- `jpeg_encode_fail = 0`.
- `jpeg_soi_0 = 0xFF`, `jpeg_soi_1 = 0xD8`, `jpeg_eoi_0 = 0xFF`, `jpeg_eoi_1 = 0xD9`.
- `uvc_class_iso_frame_drops` не растет быстро.
- В pcap нет `XACT_ERROR`.
- По результату прогона пользователя стрим есть, качество нормальное.

Вывод: цепочка `fb -> downsample -> JPEG HW -> UVC` работает при согласованном размере `160x120`.

### Live JPEG на размере дисплея 320x480

Следующий включенный слой:

- `UVC_FRAME_WIDTH = 320`.
- `UVC_FRAME_HEIGHT = 480`.
- `UVC_MAX_FRAME_SIZE = 128 KiB`.
- `JPEG_BUF_SIZE = UVC_MAX_FRAME_SIZE`.
- `UVC_IN_PACKET = 512` остается без изменений.
- `UVC_FRAME_INTERVAL_100NS = 2000000`, то есть 5 fps, остается без изменений.
- USB DMA и cache не включаются.
- Дисплей, SDRAM, MDMA/JPEG и live pipeline остаются включенными.

Ожидание в отладчике:

- `uvc_desc_frame_width_dbg = 320`.
- `uvc_desc_frame_height_dbg = 480`.
- `uvc_desc_frame_max_dbg = 131072`.
- `uvc_probe_dbg_max_frame = 131072`.
- `uvc_commit_dbg_max_frame = 131072`.
- `camera_pipeline_uvc_w_dbg = 320`.
- `camera_pipeline_uvc_h_dbg = 480`.
- `jpeg_live_src_w_dbg = 320`.
- `jpeg_live_src_h_dbg = 480`.
- `camera_pipeline_encode_ok` растет.
- `camera_pipeline_skip_oversize_jpeg = 0`.
- `jpeg_encode_fail = 0`.
- `uvc_class_iso_frame_drops` не растет быстро.

Если этот слой стабилен, следующий шаг: уже не USB, а оптимизация качества/размера JPEG и фактического FPS.

Наблюдение после первого запуска:

- Кадры приходят целыми, без старых артефактов.
- Стрим через некоторое время останавливается.
- Цвета стартовой таблицы, нарисованной через DMA2D, были неверными; при прямой записи в framebuffer цвета нормальные.

Причина проблемы цвета:

- В `DMA2D_R2M` HAL принимает параметр `pdata` как `ARGB8888`, даже если `hdma2d.Init.ColorMode = DMA2D_OUTPUT_RGB565`.
- Мы передавали в `HAL_DMA2D_Start()` уже готовые `RGB565` значения (`0xF800`, `0x07E0`, ...), поэтому HAL повторно преобразовывал не тот формат.

Исправление:

- Перед `HAL_DMA2D_Start()` конвертировать `RGB565 -> ARGB8888`.
- Диагностика: `dma2d_last_rgb565_color`, `dma2d_last_argb8888_color`.

Для остановки стрима добавлена диагностика producer/JPEG:

- `camera_pipeline_last_update_tick`.
- `camera_pipeline_last_encode_start_tick`.
- `camera_pipeline_last_encode_ms`.
- `camera_pipeline_max_encode_ms`.
- `camera_pipeline_last_encode_ok_tick`.
- `camera_pipeline_last_encode_fail_tick`.
- `camera_pipeline_last_submit_ok_tick`.
- `camera_pipeline_last_submit_fail_tick`.
- `camera_pipeline_last_oversize_jpeg_size`.
- `camera_pipeline_last_oversize_tick`.

Если стрим остановится снова, сравнивать:

- растет ли `camera_pipeline_encode_ok`;
- растет ли `camera_pipeline_submit_ok`;
- растет ли `uvc_frames_sent`;
- не растет ли `camera_pipeline_skip_oversize_jpeg`;
- не растет ли `jpeg_encode_fail`;
- не растет ли `uvc_class_iso_frame_drops`;
- меняется ли `pcd_datain_ep1_calls`.

Результат после 320x480:

- Цвета экрана исправлены после конвертации `RGB565 -> ARGB8888` для `DMA2D_R2M`.
- Стрим стартует не каждый раз.
- По отладчику: `uvc_frames_sent = 1`, `pcd_datain_ep1_calls = 430`, `pcd_isoin_incomplete_calls` и `uvc_class_iso_frame_drops` растут сотнями тысяч.
- `camera_pipeline_encode_ok = 3`, `camera_pipeline_submit_ok = 3`, `camera_pipeline_skip_pending` растет очень быстро.
- `jpeg_encode_fail = 0`, `camera_pipeline_skip_oversize_jpeg = 0`.
- `camera_pipeline_max_encode_ms = 291`, что больше заявленного frame interval 200 ms.

Вывод:

- JPEG encoder сам не падает и кадры не превышают `UVC_MAX_FRAME_SIZE`.
- Новый отказ похож на scheduling/starvation: после первого кадра endpoint попадает в ISO incomplete storm, pending live JPEG не коммитится, producer упирается в `pending_valid`.

Новый фикс:

- Первый вариант `uvc_class_iso_requeue_enable = 1` оказался слишком агрессивным: requeue из самого incomplete-callback успешно возвращал `OK`, но поток все равно уходил в бесконечный ISO incomplete storm.
- `uvc_class_iso_requeue_enable` теперь выключен по умолчанию.
- После `UVC_DropFrameOnIsoIncomplete()` class переводится в `UVC_STATE_READY`, чтобы следующий стартовый header-only packet ставился из `SOF`, а не из ISR.
- Если incomplete приходит без активного MJPEG-кадра, он не должен дропать ничего: добавлен счетчик `uvc_class_iso_incomplete_no_active_frame`.
- Добавлены счетчики: `uvc_class_iso_requeue_calls`, `uvc_class_iso_requeue_ok`, `uvc_class_iso_requeue_fail`, `uvc_class_iso_requeue_last_status`.
- В live branch после `camera_pipeline_init()` подготавливается и коммитится первый live JPEG до основного цикла.
- Для 320x480 временно снижен заявленный frame interval до `500 ms` (`2 fps`), потому что `camera_pipeline_max_encode_ms = 291` больше прежнего `200 ms`.

Ожидание:

- При неудачном/позднем кадре `pcd_isoin_incomplete_calls` может немного расти, но не должен уходить в сотни тысяч.
- `uvc_class_iso_requeue_*` не должны расти при `uvc_class_iso_requeue_enable = 0`.
- `uvc_class_iso_incomplete_no_active_frame` может расти, если incomplete приходит между кадрами, но это не должно сбрасывать producer.
- `uvc_frames_sent` должен продолжать расти после первого кадра.

## 2026-04-28: новый сбой после 320x480 live JPEG

Свежий pcap после сообщения "стрима нет":

- UVC-устройство найдено как `0483:5750` на USBPcap адресе `1/40`.
- На EP `0x81` видны один-два коротких MJPEG кадра около `4.42 s` и `4.82 s`.
- Между кадрами хост получает большое количество 2-байтных UVC header-only пакетов.
- После этого появляются ISO URB с `iso_error_count = 23`, затем URB с `iso_error_count = 128` и нулевой полезной длиной: поток фактически ушел в idle/incomplete storm.

Счетчики из отладчика:

- `uvc_frames_sent = 3`;
- `pcd_isoin_incomplete_calls = 78968`;
- `uvc_class_iso_frame_drops = 0`;
- `uvc_class_iso_requeue_calls = 0`;
- `camera_pipeline_encode_ok = 5`;
- `camera_pipeline_submit_ok = 5`;
- `camera_pipeline_skip_pending` быстро растет.

Вывод:

- JPEG продолжает кодироваться и submit проходит.
- Producer упирается в pending frame, потому что UVC перестает регулярно коммитить следующий кадр.
- Сейчас сбой больше похож не на порчу JPEG, а на неудачный старт/рестарт ISO после READY/idle: SOF-ветка стартовала передачу отдельным 2-байтным header-only пакетом, а первый настоящий payload мог уже не запуститься после incomplete.

Новая правка:

- В STM-like `USBD_UVC_SOF()` старт потока теперь идет через `UVC_STM_ExampleDataIn()`, то есть через тот же путь, что и обычный `DataIn`.
- Отдельный `uvc_stm_sof_start_packet` удален.
- Идея: первый пакет после READY должен быть настоящим пакетом текущего JPEG, если кадр доступен, а не standalone header-only packet.

Что проверить после прошивки:

- `uvc_stm_exact_datain_calls` должен расти сразу после `uvc_stm_sof_start_calls`.
- `uvc_stm_exact_last_size` на старте должен быть больше `2`, если текущий JPEG доступен.
- `uvc_frames_sent` должен продолжать расти.
- `pcd_isoin_incomplete_calls` не должен уходить в десятки тысяч за секунды.
- `uvc_class_iso_incomplete_no_active_frame` полезно добавить в watch: если он растет вместе с `pcd_isoin_incomplete_calls`, значит следующий подозреваемый - idle/header-only режим между кадрами.

Результат проверки:

- Стрима нет.
- `uvc_stm_exact_datain_calls = 99179`, но `uvc_stm_exact_last_size = 2`.
- `uvc_frames_sent = 1`.
- `pcd_isoin_incomplete_calls = 94857`.
- `uvc_class_iso_incomplete_no_active_frame = 94852`.

Вывод:

- Эксперимент с SOF-стартом через `UVC_STM_ExampleDataIn()` не помог.
- Он подтвердил картину no-active/header-only storm: `DataIn` вызывается, но почти всегда получает только 2-байтный пустой пакет, потому что активного кадра у UVC backend нет.

Откат к последнему рабочему слою:

- Вернули live UVC размер `160x120`.
- Вернули `UVC_MAX_FRAME_SIZE = 8192`.
- Вернули `UVC_FRAME_INTERVAL_100NS = 2000000` (`5 fps`).
- Вернули STM-like SOF start через отдельный 2-байтный start packet, как было в рабочем слое.
- Дисплей `320x480`, SDRAM, MDMA/JPEG и live pipeline остаются включенными; pipeline снова downsample-ит framebuffer до `160x120`.

Если этот откат снова дает стабильный стрим, дальнейший план:

- Зафиксировать это как текущий рабочий baseline.
- Поднимать размер не сразу до `320x480`, а ступенями: сначала `320x240`, потом `320x480`.
- На каждой ступени смотреть, где именно начинается no-active/header-only storm: после первого кадра, между кадрами, или при ожидании нового JPEG.

## 2026-04-28: падение после долгого 160x120 live-прогона

Свежий pcap после сообщения "с первого раза стрим упал почти сразу, второй раз проработал довольно долго":

- UVC-устройство найдено как `0483:5750` на USBPcap адресе `1/16`.
- Захват длится около `30.64 s`; рабочая передача видна примерно с `3.99 s` до `21.00 s`.
- Между настоящими MJPEG URB (`1668`/`1678` байт примерно раз в `200 ms`) идут регулярные URB по `256` байт: это `128` пакетов по `2` байта, то есть чистый UVC header-only idle.
- Первая критическая ошибка: frame `29479`, время `21.003534 s`, `data_len = 42`, `iso_error_count = 107`.
- После нее идут URB с `data_len = 0` и `iso_error_count = 128`; в отладчике одновременно растет `uvc_class_iso_incomplete_no_active_frame`.

Вывод:

- Это не похоже на порчу JPEG: перед падением реальные MJPEG пакеты идут целыми, а сбой начинается не с payload-кадра, а с idle/header-only области.
- Наш код между кадрами слишком активно поддерживал ISO поток пустыми 2-байтными пакетами. После одного плохого ISO URB класс оказывался в состоянии "активного кадра нет", но USB продолжал получать/ожидать пустые scheduled transfers, что превращалось в no-active/incomplete storm.

Текущая собранная правка:

- `UVC_Itf_DataMinimal()` больше не возвращает бесконечный `UVC_HEADER_SIZE`, когда нет pending JPEG; вместо этого возвращает `psize = 0`.
- После EOF кадра backend переводится в ожидание следующего pending кадра, без автоматического 2-байтного пакета в `DataIn`.
- `UVC_STM_ExampleDataIn()` обрабатывает `PcktSze == 0` как no-tx: возвращает класс в `UVC_STATE_READY` и не ставит новый USB transfer.
- `USBD_UVC_SOF()` в STM-like ветке не стартует standalone header-only пакет, пока producer свободен и нового pending кадра нет.

Что проверить после прошивки этой сборки:

- `uvc_frames_sent` должен продолжать расти после десятков кадров.
- `uvc_class_iso_incomplete_no_active_frame` не должен уходить в сотни тысяч.
- `uvc_stm_header_only_packets` должен резко уменьшиться.
- Новые полезные watch-переменные: `uvc_min_no_tx_wait_pending`, `uvc_stm_no_tx_packets`, `uvc_stm_sof_wait_pending_calls`.
- В pcap между настоящими MJPEG URB не должно быть постоянного ряда `data_len = 256` каждые `16 ms`.

Результат проверки no-header-idle:

- Приложение не показало стабильный старт стрима.
- В pcap на адресе `1/20` данные на EP `0x81` все же появлялись, но между ними были длинные участки zero-length ISO URB.
- Затем около `9.35 s` снова начался ISO storm: `iso_error_count = 111`, затем `iso_error_count = 128`.

Уточнение:

- Полностью молчать между кадрами тоже нельзя: хост/USB scheduling плохо переживает длинные участки без IN data.
- Старый режим был другой крайностью: 2-байтный header-only packet на каждый microframe.

Новая правка:

- В STM-like `USBD_UVC_SOF()` добавлен промежуточный режим.
- Если активный кадр уже закончен, а нового pending JPEG еще нет, класс не молчит полностью, но и не гонит бесконечный self-loop.
- SOF отправляет только редкий 2-байтный keepalive по `uvc_frame_keepalive_interval_ms`.
- Сейчас `uvc_frame_keepalive_interval_ms = 8`, то есть ожидается примерно один keepalive за несколько миллисекунд, а не `128` header-only пакетов в каждом URB.

Что смотреть после прошивки:

- `uvc_frame_keepalive_packets` должен расти умеренно.
- `uvc_stm_header_only_packets` должен быть значительно меньше, чем раньше.
- `uvc_stm_sof_wait_pending_calls` может расти: это нормальное ожидание нового кадра.
- `uvc_frames_sent` должен расти, а `uvc_class_iso_incomplete_no_active_frame` не должен уходить в storm.

Результат:

- После прошивки пользователь сообщил, что устройство перестало определяться хостом.
- Теоретически эта правка не должна влиять на enumeration, потому что находится в SOF-пути после `SET_INTERFACE alt=1`, но для исключения риска эксперимент сразу откатан.
- Сборка после отката успешна.

Дальше:

- Прошить сборку после отката редкого keepalive.
- Сделать полный power-cycle платы и переподключить USB.
- Если устройство снова определяется, следующий рабочий путь - не idle/keepalive, а двойной/тройной кадровый буфер: UVC всегда повторяет последний готовый JPEG, пока producer кодирует новый.

Уточнение от пользователя:

- Плата каждый раз полностью выключается, поэтому зависшее состояние после soft reset исключено.

Аварийная проверка enumeration:

- Для отделения USB enumeration от остальной периферии собрана `enumeration-safe` конфигурация.
- Отключены только слои выше USB: `main_sdram_enable = 0`, `main_display_enable = 0`, `main_jpeg_hw_enable = 0`, `main_live_jpeg_enable = 0`.
- USB остается ранним: `main_usb_init_early = 1`.
- UVC источник возвращается к тестовым JPEG из прошивки.

Цель:

- Если эта сборка определяется хостом, значит enumeration и USB descriptors живы, а отказ связан с дальнейшей периферией или live pipeline.
- Если даже эта сборка не определяется, смотреть уже низкий уровень USB init/clock/ULPI/descriptors, а не JPEG/display.

Результат pcap после `enumeration-safe` сборки:

- Устройство снова определяется как `0483:5750`, USBPcap адрес `1/30`.
- Значит descriptors/enumeration/USB init живы; проблема не в базовом перечислении.
- `SET_INTERFACE alt=1` виден около `1.914 s`.
- На EP `0x81` появляются данные тестового JPEG, но около `4.943821 s` снова начинается ISO failure:
  - первый сбой: `data_len = 0`, `iso_error_count = 102`;
  - дальше `data_len = 0`, `iso_error_count = 128`.
- Так как SDRAM/display/JPEG/live отключены, причина теперь точно внутри текущей UVC state machine/подачи кадров, а не в верхней периферии.

Следующая правка:

- Реализована идея "если новый кадр не готов, повторять последний готовый".
- В `UVC_Itf_DataMinimal()` отсутствие pending кадра больше не возвращает `psize = 0`; вместо этого класс заново загружает `video_source_get_current_frame()`.
- В STM-like `USBD_UVC_SOF()` убран early return при `video_source_can_accept_frame()`: отсутствие pending теперь не блокирует старт следующего логического кадра.
- Добавлен счетчик `uvc_min_repeat_last_frames`.

Что проверить:

- `uvc_min_repeat_last_frames` должен расти, когда producer не успевает/не работает.
- `uvc_frames_sent` должен расти постоянно.
- `uvc_class_iso_incomplete_no_active_frame` не должен уходить в storm.
- Если эта сборка стабильна на тестовых JPEG, возвращать SDRAM/display/JPEG/live слоями обратно.

Уточнение после отказа enumeration:

- Прямой repeat-хак внутри UVC оказался неудачным: пользователь сообщил, что устройство снова перестало определяться.
- Этот вариант считаем откатанным как архитектурно неправильный: UVC class не должен сам гадать про владение кадрами.

Новая реализация double/latest buffering:

- Повтор последнего кадра перенесен в `video_source`.
- Добавлена функция `video_source_prepare_next_frame(bool *repeated)`:
  - если pending кадр есть, он коммитится как новый current;
  - если pending нет, но current валиден, источник разрешает повтор current;
  - если нет ни pending, ни current, возвращается `false`.
- Добавлена функция `video_source_has_current_frame()`.
- `UVC_Itf_DataMinimal()` теперь вызывает `video_source_prepare_next_frame()` вместо ручной логики pending/no-pending.
- `USBD_UVC_SOF()` снова имеет защитный early return, но только когда нет pending и нет current кадра для повтора.
- Добавлены debug-счетчики `dbg_prepare_next_calls`, `dbg_repeat_current_calls`, `uvc_min_repeat_last_frames`.
- Верхние слои пока остаются выключенными (`main_sdram_enable = 0`, `main_display_enable = 0`, `main_jpeg_hw_enable = 0`, `main_live_jpeg_enable = 0`), чтобы сначала проверить enumeration и стабильность на тестовых JPEG.

Что проверить:

- Устройство должно снова определяться.
- `uvc_frames_sent` должен расти.
- При отсутствии нового pending кадра должны расти `dbg_repeat_current_calls` и `uvc_min_repeat_last_frames`.
- Если это стабильно, следующий шаг - включать SDRAM, display, JPEG HW и live JPEG обратно по одному.

## 2026-04-28: pcap после double/latest, камера определяется без отладки, но стрима нет

Симптом:

- Под отладчиком камера иногда не успевает определиться. Это ожидаемо для USB, если ядро останавливается на reset/main или на раннем breakpoint: enumeration имеет жесткие тайминги.
- Без отладки устройство определяется как `0483:5750`, но приложение не показывает поток.

Факты из `UVC.pcapng`:

- Устройство `1/50`, `SET_INTERFACE alt=1` виден на `3.067412 s`.
- EP `0x81` реально активен; после `alt=1` идут ISO URB с данными.
- В разобранном участке нет признака транспортной ISO-потери: `iso_error_count = 0`.
- Но содержимое пакетов некорректно для UVC/MJPEG: много ISO descriptors содержат сырой JPEG payload без UVC payload header `02 xx`.
- EOF-бит в таких пакетах не восстанавливается как ожидаемый конец MJPEG кадра, поэтому хост получает байты, но не валидный UVC кадр.

Рабочий вывод:

- Это уже не проблема Windows и не чистая потеря USB-пакетов.
- Текущая причина "нет стрима" - неверная упаковка UVC payload после последних экспериментов с repeat/latest: часть передач уходит как JPEG-данные без обязательного 2-байтного UVC header.
- Дополнительная ошибка: repeat последнего кадра был разрешен без привязки к `UVC_FRAME_INTERVAL_MS`, поэтому при отсутствии pending кадра UVC мог повторять current почти на каждый SOF, намного быстрее заявленных `5 fps`.

Правка:

- Double/latest buffering оставлен в `video_source`, но повтор current теперь запускается только после истечения `uvc_frame_interval_ms`.
- В `UVC_Itf_DataMinimal()` добавлено ожидание frame interval; пока интервал не вышел, `Data` возвращает `psize = 0`, а не начинает новый кадр.
- В STM-like `USBD_UVC_SOF()` добавлено такое же ожидание interval с редким keepalive через `uvc_frame_keepalive_interval_ms`.
- В `UVC_STM_ExampleDataIn()` убрана прямая отправка prebuilt/raw указателя. Теперь каждый ISO packet заново собирается в локальный `packet[]`: байты `UVC_HEADER_SIZE`, flags/FID/EOF, затем payload. Это должно убрать ситуацию, где на шину уходит сырой JPEG без `02 xx`.
- Кэш и USB DMA не включались.

Что проверить после прошивки:

- Устройство должно определяться и под обычным запуском, и при отладке после `Run` без ранних breakpoints.
- В pcap каждый непустой ISO descriptor EP `0x81` должен начинаться с `02 00/01/02/03` или близкого валидного UVC header.
- `uvc_frames_sent` должен расти примерно с темпом frame interval, а не тысячами в секунду.
- `dbg_repeat_current_calls` и `uvc_min_repeat_last_frames` могут расти, если producer не дает новый кадр, но это не должно ломать UVC packet headers.

Результат проверки:

- Пользователь сообщил: стрим пошел.
- Debug-счетчики на момент проверки:
  - `uvc_frames_sent = 42`;
  - `dbg_prepare_next_calls = 41`;
  - `dbg_repeat_current_calls = 0`;
  - `uvc_min_repeat_last_frames = 0`;
  - `pcd_isoin_incomplete_calls = 18`.
- Свежий pcap: UVC-устройство `0483:5750` на адресе `1/54`, `SET_INTERFACE alt=1` на `2.713856 s`.
- На EP `0x81` разобрано `524` ISO URB, все с `iso_error_count = 0`.
- После переходного участка старта, начиная с `3.967018 s`, raw-пакетов без UVC header больше нет:
  - `after_chunks = 1168`;
  - `raw_after = 0`;
  - `frames_after = 36`;
  - exact-valid MJPEG: `36/36`;
  - размеры: `4289` байт - `18` кадров, `3085` байт - `18` кадров.
- Первый кадр до стабильного участка получился переходным/накопленным (`17245` байт, внутри несколько SOI). После него поток становится чистым.

Вывод:

- Основная проблема "сырой JPEG без UVC header" исправлена для стабильного участка потока.
- `pcd_isoin_incomplete_calls` еще появляются на устройстве, но в этом pcap им не соответствуют USBPcap ISO errors, и поток восстанавливается.
- Текущую сборку считать новой рабочей точкой для minimal/test JPEG.

Дальше:

- Дать этой сборке поработать дольше на тестовых JPEG.
- Если поток не падает, возвращать верхние слои по одному: SDRAM, display, JPEG HW, live JPEG.
- При возврате live pipeline не ломать найденную схему: один собранный UVC packet = `header + payload`, repeat/latest только через `video_source`, без ускорения выше `UVC_FRAME_INTERVAL_MS`.

## 2026-04-28: возврат периферии и live JPEG

Решение:

- Пользователь сообщил, что minimal/test JPEG сборка продолжает стабильно работать, и попросил вернуть периферию сразу.
- Включен полный live-профиль:
  - `main_usb_only_mode = 0`;
  - `main_sdram_enable = 1`;
  - `main_display_enable = 1`;
  - `main_jpeg_hw_enable = 1`;
  - `main_live_jpeg_enable = 1`.
- USB-путь не менялся:
  - кэш не включался;
  - USB DMA не включался;
  - UVC packetization остается через единый `header + payload` TX packet.
- `main_test_pattern_enable` оставлен `0`: дисплей стартует с начальным framebuffer-паттерном, live JPEG кодирует framebuffer через JPEG HW.

Что проверить:

- Инициализация слоев: `main_sdram_init_done`, `main_display_init_done`, `main_jpeg_init_done`, `main_live_jpeg_init_done`.
- SDRAM smoke test: `main_sdram_test_ok = 1`.
- JPEG pipeline: `camera_pipeline_encode_ok`, `camera_pipeline_submit_ok`, `camera_pipeline_last_jpeg_size`, `camera_pipeline_max_encode_ms`.
- UVC: `uvc_frames_sent` должен расти, `uvc_min_repeat_last_frames` может расти только если JPEG producer не успевает.
- Если поток опять пропадет, первым делом сравнить pcap: сохраняется ли правило "каждый ISO payload начинается с UVC header", или проблема стала уже в producer/JPEG timing.

## 2026-04-28: live JPEG, поток стартует и затем пропадает

Симптом:

- Пользователь сообщил: дисплей ожил, UVC-стрим есть, но спустя некоторое время пропадает.
- Пакеты до остановки визуально дают целую картинку, без прежних битых полос и пропавших chunks.
- Нельзя сразу добавлять повторные перезапуски/лечилки: сначала нужно понять, поток падает из-за producer/JPEG или из-за состояния ISO endpoint.

Факты из свежего `UVC.pcapng`:

- Актуальное UVC-устройство: `0483:5750`, USBPcap адрес `1/56`.
- `SET_INTERFACE alt=1` на `6.724883 s`, `SET_INTERFACE alt=0` от хоста на `16.124753 s`.
- Полезные данные EP `0x81` идут с `6.745652 s` до `10.345642 s`.
- Реконструировано `18` MJPEG-кадров: `17` валидных, `1` переходный стартовый неполный.
- Последний валидный кадр: `10.233645..10.249649 s`, размер `1412` байт, `3` UVC chunks.
- После последнего валидного кадра до `10.345642 s` идут только header-only keepalive-пакеты.
- На `10.345642 s` виден URB с header-only данными и `iso_error_count = 39`.
- Начиная с `10.361642 s` идут zero-length/error ISO события (`0xc0000b00`, затем много `0xc0010000`), после чего хост еще несколько секунд держит интерфейс и затем выключает поток через `alt=0`.

Вывод:

- В этом pcap не видно порчи JPEG-пакетов как первичной причины. Пока payload есть, UVC header и JPEG собираются корректно.
- Провал начинается в паузе между кадрами: после валидного кадра реальный payload закончился, до следующего кадра по `uvc_frame_interval_ms = 200` ms еще далеко, а endpoint уходит в ISO error/no-accessed состояние во время keepalive-only участка.
- Гипотеза "JPEG producer не успевает" остается рабочей, но pcap показывает более точную форму: starvation/ожидание кадра создает длинное окно без payload, и именно в этом окне ломается ISO state.
- Следующий шаг - не менять сборку MJPEG, а измерить момент: был ли активный frame, ждали ли `next_frame_tick`, что вернул keepalive transmit, и какие регистры EP1 были в момент `IsoINIncomplete`.

Добавленная диагностика без изменения поведения:

- `uvc_keepalive_tx_calls`, `uvc_keepalive_tx_ok`, `uvc_keepalive_tx_fail`.
- `uvc_keepalive_last_status`, `uvc_keepalive_last_tick`, `uvc_keepalive_last_state`, `uvc_keepalive_last_next_frame_tick`.
- `uvc_keepalive_last_diepctl`, `uvc_keepalive_last_dieptsiz`, `uvc_keepalive_last_diepint`.
- `uvc_iso_no_active_wait_interval`.
- `uvc_iso_no_active_last_tick`, `uvc_iso_no_active_last_state`, `uvc_iso_no_active_last_next_frame_tick`.
- `uvc_iso_no_active_last_diepctl`, `uvc_iso_no_active_last_dieptsiz`, `uvc_iso_no_active_last_diepint`.

Что проверить на следующем прогоне:

- Если при остановке растут `uvc_iso_no_active_wait_interval` и `uvc_class_iso_incomplete_no_active_frame`, значит ISO incomplete возникает именно в окне ожидания следующего кадра, а не посреди отправки JPEG.
- Если `uvc_keepalive_tx_fail = 0`, но в pcap появляются `iso_error_count`, значит `USBD_LL_Transmit` принимает передачу, но железо/хост не забирает ISO descriptor.
- Если `camera_pipeline_encode_ok` и `camera_pipeline_submit_ok` перестают расти задолго до падения USB, тогда копаем live JPEG producer.
- Если producer продолжает расти, а ISO падает только на keepalive, отдельно проверять стратегию паузы между маленькими кадрами, не трогая формат MJPEG payload.

Результат проверки:

- Пользовательский debug-снимок после падения:
  - `uvc_keepalive_tx_calls = 1032`;
  - `uvc_keepalive_tx_ok = 1032`;
  - `uvc_keepalive_tx_fail = 0`;
  - `uvc_keepalive_last_status = 0`;
  - `uvc_iso_no_active_wait_interval = 1510`;
  - `uvc_class_iso_incomplete_no_active_frame = 46208`;
  - `uvc_frames_sent = 43`;
  - `camera_pipeline_encode_ok = 45`;
  - `camera_pipeline_submit_ok = 45`;
  - `camera_pipeline_skip_jpeg_busy = 0`;
  - `jpeg_encode_fail = 0`;
  - `jpeg_fail_timeout = 0`.
- Свежий pcap: UVC-устройство `1/58`, `SET_INTERFACE alt=1` на `4.415200 s`, `alt=0` на `18.594059 s`.
- Реконструировано `42` MJPEG-кадра: `41` валидный, `1` стартовый неполный.
- Последний валидный кадр закончился на `12.739943 s`, размер `1412` байт.
- После него до первого ISO error идут только header-only keepalive:
  - `12.755950`: `0280,0280`;
  - `12.771943`: `0280,0280`;
  - `12.787946`: `0280,0280`;
  - `12.803943`: `0280,0280`;
  - `12.819940`: `0280,0280`;
  - `12.835940`: `0280,0280,0280,0280`, `iso_error_count = 71`.
- После `12.851937 s` идут zero-length/error ISO события (`0xc0000b00`, `0xc0010000`).

Уточненный вывод:

- JPEG producer не является первичной причиной этой остановки: он продолжает кодировать и submit-ить кадры, аппаратных/timeout ошибок JPEG нет.
- `USBD_LL_Transmit` для keepalive не сообщает ошибку, но хост/USBPcap показывает ISO errors; значит проблема ниже уровня возврата `USBD_LL_Transmit`.
- Корень текущего сценария - слишком длинное пустое окно между очень маленькими MJPEG-кадрами. При `5 fps` live JPEG занимает 2-3 ISO packets, затем почти `200 ms` поток живет на редких header-only keepalive. Именно там начинается `IsoINIncomplete` storm.

Следующая проверка без recovery-костылей:

- Разделить частоту USB-транспорта и частоту подготовки JPEG.
- UVC descriptor/probe/commit перевести на `30 fps`:
  - `UVC_FRAME_INTERVAL_100NS = 333333`;
  - `UVC_FRAME_RATE = 30`.
- JPEG producer оставить на `5 fps`:
  - `UVC_PRODUCER_INTERVAL_MS = 200`;
  - `camera_pipeline_period_ms = UVC_PRODUCER_INTERVAL_MS`.
- Ожидаемое поведение: USB чаще повторяет последний готовый кадр, поэтому длинное keepalive-only окно исчезает; JPEG-кодек при этом не получает новую нагрузку.

Результат проверки `30 fps` UVC / `5 fps` producer:

- Debug-снимок:
  - `uvc_keepalive_tx_calls = 248`;
  - `uvc_keepalive_tx_ok = 248`;
  - `uvc_keepalive_tx_fail = 0`;
  - `uvc_iso_no_active_wait_interval = 7`;
  - `uvc_class_iso_incomplete_no_active_frame = 119814`;
  - `uvc_frames_sent = 62`;
  - `camera_pipeline_encode_ok = 14`;
  - `camera_pipeline_submit_ok = 14`;
  - `jpeg_encode_fail = 0`;
  - `jpeg_fail_timeout = 0`.
- pcap: UVC-устройство `1/60`, `SET_INTERFACE alt=1` на `1.955020 s`, `alt=0` на `9.304081 s`.
- После переходного старта восстановлено `58` валидных MJPEG кадров подряд.
- Валидные live JPEG имеют размеры только `1412` и `1422` байт.
- Длинный keepalive-only участок почти исчез: `uvc_iso_no_active_wait_interval` упал до `7`.
- Новый первый сбой происходит уже не в паузе ожидания, а на втором payload-пакете очередного кадра:
  - последний полный кадр: `3.960014..3.976021 s`, `1412` байт;
  - следующий кадр стартует на `3.992012 s`;
  - на `4.008019 s` приходит payload chunk `512` байт с `iso_error_count = 68`;
  - следующий ожидаемый EOF chunk уже не приходит, дальше начинается zero-length/error storm.

Уточненный вывод:

- Ускорение UVC до `30 fps` подтвердило часть гипотезы: проблема длинного пустого ожидания была реальной.
- Но первопричина еще глубже: при слишком маленьком live JPEG поток все равно очень разреженный для HS ISO IN, и единичный пропуск ISO payload переводит endpoint в storm.
- Это согласуется с прежним наблюдением пользователя: более крупные JPEG кадры шли заметно лучше.

Следующая проверка:

- Не включать recovery/requeue/reopen.
- Не включать кэш и USB DMA.
- Поднять только размер live JPEG через качество кодирования:
  - `jpeg_live_quality = 50`.
- Цель: проверить, исчезнет ли ISO storm, когда кадр станет ближе к прежним `3..4 KB` вместо `~1.4 KB`.
- Смотреть `camera_pipeline_last_jpeg_size`, `camera_pipeline_skip_oversize_jpeg`, `uvc_frames_sent`, `uvc_class_iso_frame_drops`, `uvc_class_iso_incomplete_no_active_frame`.

Результат проверки `jpeg_live_quality = 50`:

- Пользователь сообщил: устройство перестало распознаваться хостом.
- Debug-снимок:
  - `camera_pipeline_last_jpeg_size = 1894`;
  - `camera_pipeline_skip_oversize_jpeg = 0`;
  - `uvc_frames_sent = 0`;
  - `uvc_class_iso_frame_drops = 0`;
  - `uvc_class_iso_incomplete_no_active_frame = 0`.
- pcap не содержит нашего UVC `0483:5750` вообще. В захвате виден только ST-Link `0483:3748` и другие устройства.
- Значит хост не дошел даже до чтения device/config descriptor UVC-камеры. Это не похоже на отказ из-за MJPEG payload, потому что stream interface не стартовал.
- Последнее изменение `jpeg_live_quality = 50` откатываем до `10`, чтобы вернуться к последней конфигурации, где устройство распознавалось и стрим стартовал.

Следующее действие:

- Проверить, вернулось ли распознавание после отката качества.
- Если устройство снова не распознается, искать причину в USB init/подключении/порядке старта, а не в JPEG качестве.

## 2026-04-28: сверка descriptor/probe/commit и первого JPEG перед USB

Проверка согласованности UVC-параметров:

- Descriptor MJPEG frame использует те же макросы, что и runtime:
  - `UVC_FRAME_WIDTH = 160`;
  - `UVC_FRAME_HEIGHT = 120`;
  - `UVC_MAX_FRAME_SIZE = 8192`;
  - `UVC_FRAME_INTERVAL_100NS = 333333`;
  - `UVC_FRAME_RATE = 30`;
  - `UVC_FRAME_BITRATE = UVC_MAX_FRAME_SIZE * 8 * UVC_FRAME_RATE`;
  - `UVC_IN_PACKET = 512`;
  - `UVC_HS_EP_INTERVAL = 1`.
- `UVC_InitDefaultProbeCommit()` и `UVC_NormalizeProbeCommit()` выставляют:
  - `dwFrameInterval = UVC_FRAME_INTERVAL_100NS`;
  - `dwMaxVideoFrameSize = video_source_get_max_frame_size() = 8192`;
  - `dwMaxPayloadTransferSize = UVC_GetPayloadTransferSize() = 512`.
- Значит descriptor, GET_CUR/GET_MIN/GET_MAX/GET_DEF и SET_CUR-normalize сейчас согласованы по frame interval, max frame и packet size.
- Runtime millisecond pacing был `333333 / 10000 = 33 ms`, то есть чуть быстрее заявленных `30 fps`. Исправлено на округление вверх:
  - `UVC_FRAME_INTERVAL_MS = (UVC_FRAME_INTERVAL_100NS + 9999) / 10000`;
  - для `333333` это `34 ms`, то есть прошивка не обещает `30 fps`, а фактически не шлет быстрее этого.

Проверка первого JPEG:

- Найден риск в порядке старта: `main_usb_init_early = 1` поднимал USB до SDRAM/display/JPEG/live pipeline.
- Затем `camera_pipeline_init()` повторно вызывал `video_source_init()` уже при живом USB, что могло сбросить источник кадров во время enumeration/stream start.
- Исправлено:
  - `main_usb_init_early = 0`;
  - USB стартует после `camera_pipeline_init()`, первого `camera_pipeline_update()` и `video_source_commit_pending_if_any()`;
  - перед `MX_USB_DEVICE_Init()` сохраняются debug-снимки:
    - `main_first_jpeg_ready_before_usb`;
    - `main_first_jpeg_size_before_usb`;
    - `main_first_jpeg_ptr_before_usb`.
- Если по какой-то причине кадра нет, перед USB init выполняется fallback `video_source_init()`, чтобы UVC не стартовал с пустым источником.

Что проверить после прошивки:

- При старте до enumeration:
  - `main_first_jpeg_ready_before_usb = 1`;
  - `main_first_jpeg_size_before_usb != 0`;
  - при live JPEG желательно увидеть размер live-кадра, а не только встроенного fallback.
- После подключения:
  - устройство снова должно появиться как `0483:5750`;
  - `uvc_probe_dbg_interval = 333333`;
  - `uvc_commit_dbg_interval = 333333`;
  - `uvc_probe_dbg_max_payload = 512`;
  - `uvc_commit_dbg_max_payload = 512`;
  - `uvc_desc_ep_mps_dbg = 512`;
  - `uvc_desc_ep_interval_dbg = 1`.

## 2026-04-28: pcap после зависания live-стрима, проверка темпа и STM-like header-only

Свежий `UVC.pcapng`:

- UVC-устройство: `1/17`, `0483:5750`.
- `SET_INTERFACE alt=1`: `1.984358 s`.
- `SET_INTERFACE alt=0`: `10.175915 s`.
- Восстановлено `82` MJPEG-frame участка, из них `80` валидных JPEG подряд.
- Валидные размеры: `1412` и `1422` байта.
- Первый валидный кадр: `2.133817 s`, последний валидный кадр: `4.821812 s`.
- После последнего валидного кадра payload больше не приходит: последний data event `4.837810 s` содержит только header-only `0280` и уже имеет `iso_error_count = 46`.
- Следующий ISO event `4.853808 s`: `data_len = 0`, `iso_error_count = 128`, `usbd_status = 0xc0000b00`.
- До `SET_INTERFACE alt=0` идет storm zero-length/error ISO events.

Вывод по вопросу "шлем много или мало":

- Мы не перегружаем USB по bitrate: `~1.4 KB * 30 fps` - это очень мало для HS ISO.
- Мы также не видим порчи JPEG: кадры до момента сбоя целые `FF D8 ... FF D9`.
- Сбой начинается не с битого payload, а с исчезновения payload и перехода endpoint/host URB в ISO error storm.
- Значит главная проблема сейчас не JPEG и не заявленный bitrate, а то, как мы поддерживаем изохронный IN endpoint между маленькими кадрами.

Найденная существенная разница с STM:

- В STM `USBD_VIDEO_DataIn()` вызывает `Data()` и затем всегда продолжает `USBD_LL_Transmit()`, если streaming уже начат.
- Если приложение возвращает размер `<= 2`, STM отправляет 2-байтный UVC header-only packet и остается в `STREAMING`.
- У нас в `UVC_Itf_DataMinimal()` в нескольких местах возвращался `psize = 0`:
  - во время `wait_frame_interval`;
  - когда нет pending/current frame;
  - сразу после завершения кадра.
- После `psize = 0` наш `UVC_STM_ExampleDataIn()` ставил состояние `READY` и обрывал DataIn-цепочку. Дальше мы пытались поднимать endpoint редкими SOF/keepalive, что не соответствует STM-like path и совпадает по времени с ISO error storm.

Исправление:

- В `UVC_Itf_DataMinimal()` заменены `psize = 0` в паузах/ожидании на `psize = UVC_HEADER_SIZE`.
- То есть между реальными MJPEG payload теперь отправляется STM-like header-only packet, а не разрыв передачи.
- Cache/DMA/requeue/replay не включались.

Что проверить после прошивки:

- `uvc_stm_no_tx_packets` должен перестать расти или остаться около нуля во время нормального стрима.
- `uvc_min_header_only_packets` и `uvc_frame_keepalive_packets` могут расти - это нормально, теперь это не аварийный keepalive, а непрерывное поддержание ISO IN цепочки.
- `uvc_class_iso_incomplete_no_active_frame` должен резко уменьшиться или перестать убегать в сотни тысяч.
- В pcap после последнего реального payload не должно быть перехода в `data_len=0 / iso_error_count=128 / 0xc0000b00`.

Внешние источники, которые совпали с наблюдениями:

- ST Community по `IISOIXFR`: для isochronous IN устаревший пакет нужно сбросить/перезаписать, потому что пакет должен быть доставлен вовремя в своем microframe.
- ST reference manual для USB OTG описывает `IISOIXFR` как incomplete isochronous IN, в том числе если приложение не успело записать payload в TxFIFO до IN token; рекомендованная реакция - остановить запись, disable/NAK endpoint и затем flush/overwrite FIFO.
- Microsoft `_URB_ISOCH_TRANSFER`: `ErrorCount` - это число isochronous packet descriptors с ошибкой, а `USBD_STATUS_ISOCH_REQUEST_FAILED` означает, что все packets в isochronous request завершились ошибками. Это совпадает с нашим `iso_error_count = 128` и `0xc0000b00`.

Результат проверки STM-like header-only при `bInterval=1`:

- UVC-устройство: `1/19`, `SET_INTERFACE alt=1` на `2.098715 s`.
- Валидных JPEG после переходного хвоста: `18`.
- Header-only действительно пошли непрерывно: `5100+` 2-байтных UVC packets в pcap.
- Последний успешный участок перед падением:
  - `2.727519 s`: валидный JPEG `1422` байта;
  - `2.743555 s`: header-only block `256` байт, `128` header-only packets, без ошибок;
  - `2.759495 s`: block содержит только `44` header-only packets (`88` байт), но `iso_error_count = 84`;
  - `2.775494 s`: `data_len = 0`, `iso_error_count = 128`, `usbd_status = 0xc0000b00`.

Новый уточненный вывод:

- Полное молчание endpoint мы убрали, но при `bInterval=1` теперь прошивка обязана обслуживать ISO IN каждые `125 us`.
- Текущая debug-сборка с live JPEG/display иногда не успевает зарядить следующий 2-байтный пакет в это окно.
- Это и есть буквальный случай "packet not ready by IN token": не JPEG-буфер пустой, а USB TxFIFO/DIEPTSIZ не успели быть подготовлены к очередному микрофрейму.
- Следующая проверка - оставить `30 fps` кадра, но снизить частоту обслуживания endpoint до `bInterval=2` (`250 us`). Это честнее, чем обещать хосту `8 kHz`, если текущая сборка периодически не выдерживает такой срок.

Изменение для следующего прогона:

- `UVC_HS_EP_INTERVAL = 2`.
- `UVC_IN_PACKET = 512` оставлен.
- `UVC_FRAME_INTERVAL_100NS = 333333` и `UVC_FRAME_RATE = 30` оставлены.
- Cache/DMA/requeue/replay не включались.

Что проверить:

- `uvc_desc_ep_interval_dbg = 2`.
- В pcap число ISO descriptors в URB должно уменьшиться относительно `bInterval=1`.
- Если падение уйдет или сильно отодвинется, корень - невыполненный deadline `125 us`, а не JPEG producer.
- Если падение останется таким же, следующий тест - `bInterval=3` или аппаратная recovery-последовательность по RM: disable/NAK/flush/overwrite endpoint после `IISOIXFR`.

Результат проверки `bInterval=2`:

- Устройство стало плохо определяться хостом; пользователь смог открыть его только с третьей попытки.
- pcap: UVC `1/27`, `SET_INTERFACE alt=1` на `2.530978 s`.
- После `alt=1` не пришло ни одного байта ISO data:
  - первый endpoint event после `alt=1` - host URB без data;
  - `2.568111 s`: `data_len = 0`, `iso_error_count = 98`;
  - `2.600112 s`: `data_len = 0`, `iso_error_count = 128`, `usbd_status = 0xc0000b00`.
- Вывод: `bInterval=2` для текущего HAL/endpoint scheduling хуже, чем `bInterval=1`. Вероятно, наша odd/even frame resync и старт первого ISO packet рассчитаны на `bInterval=1`; для `bInterval=2` первый пакет не попадает в нужное service окно.

Изменение после провала `bInterval=2`:

- `UVC_HS_EP_INTERVAL` возвращен на `1`.
- Непрерывный 2-байтный header-only flood тоже убран из minimal path.
- Вместо этого `UVC_Itf_DataMinimal()` снова возвращает `psize = 0` в паузах между кадрами, но `UVC_STM_ExampleDataIn()` больше не считает это обрывом потока:
  - отправляет zero-length ISO IN transfer через `USBD_LL_Transmit(..., NULL, 0)`;
  - оставляет состояние `STREAMING`;
  - не переводит класс обратно в `READY`.
- Цель: держать ISO endpoint переармленным между кадрами, но без записи 2-байтного header-only packet в TxFIFO на каждом микрофрейме.

Новые counters:

- `uvc_stm_zlp_packets`;
- `uvc_stm_zlp_tx_ok`;
- `uvc_stm_zlp_tx_fail`;
- `uvc_stm_zlp_last_status`.

Что проверить:

- `uvc_desc_ep_interval_dbg` снова должен быть `1`.
- Во время пауз между JPEG должны расти `uvc_stm_zlp_packets` и `uvc_stm_zlp_tx_ok`.
- `uvc_stm_zlp_tx_fail` должен оставаться `0`.
- В pcap между payload кадрами должны появиться zero-length ISO packets со статусом OK, а не `0xc0000b00`.
- Если это сработает, причина была в выборе между "нет transfer вообще" и "слишком дорогой header-only transfer"; zero-length transfer окажется самым легким способом держать endpoint живым.

Результат проверки zero-length ISO IN:

- Устройство снова стало плохо определяться, стрима нет.
- pcap: UVC `1/29`, `SET_INTERFACE alt=1` на `2.647254 s`.
- Нормальных JPEG кадров нет.
- В ISO data видны только обрывки хвоста старого payload, например `66676869,01ca1f63,ffd9`, без SOI и без валидного UVC header.
- Первый data event уже идет с ошибкой: `2.667990 s`, `data_len = 19`, `iso_error_count = 60`.
- Далее `3.131994 s`: `data_len = 0`, `iso_error_count = 128`, `usbd_status = 0xc0000b00`.

Вывод:

- Zero-length ISO IN transfer в текущем non-DMA HAL/FIFO path небезопасен: вместо чистого нулевого пакета иногда просачиваются старые данные из Tx FIFO.
- Гипотеза "держать endpoint живым через ZLP" отклонена.
- ZLP-изменение откатить; counters `uvc_stm_zlp_*` убрать.

Следующее направление:

- Не продолжать менять содержимое пауз между кадрами: `psize=0`, header-only flood и ZLP уже проверены и дают разные, но плохие варианты.
- Ускорить именно нижний USB path. UVC class file уже принудительно компилируется с `#pragma GCC optimize("O2")`, но STM HAL/LL USB-файлы по makefile всё еще собираются глобальным `-O0`.
- Добавлен `#pragma GCC optimize("O2")` в:
  - `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c`;
  - `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c`.
- Цель: ускорить `HAL_PCD_IRQHandler`, `HAL_PCD_EP_Transmit`, `USB_EPStartXfer`, `USB_WritePacket` и обработку DataIn/IISOIXFR без включения cache/DMA и без изменения UVC packet semantics.

Что проверить:

- Устройство должно снова нормально перечисляться с `bInterval=1`.
- Если stream стартует, сравнить `usb_ll_transmit_cycles_max`, `usb_writepacket_cycles_max`, `uvc_lltx_cycles_max`, `uvc_datain_cycles_max` с предыдущими значениями.
- В pcap искать не новые типы пакетов, а уменьшение/исчезновение первого `iso_error_count` при тех же `1412/1422` JPEG.

## 2026-04-29: live stream стартует, затем падает на границе кадров

Свежий `UVC.pcapng` после сообщения "стрим пошел, потом упал":

- UVC-устройство в захвате: `1/47`, endpoint `0x81`.
- Видео идет с `2.166341 s` до первого срыва около `5.018810 s`.
- До срыва восстановлено `80` валидных MJPEG кадров.
- Размеры кадров стабильные: `1422` байта и `1412` байт.
- Последний целый кадр:
  - старт `4.986817 s`;
  - конец `5.002812 s`;
  - размер `1412` байт;
  - `3` UVC packets.
- Первый плохой event после него:
  - `5.018810 s`;
  - `data_len = 2`;
  - данные: `0200`.
- Следующий event:
  - `5.034798 s`;
  - `data_len = 0`;
  - `iso_error_count = 128`;
  - `IRP USBD_STATUS = 0xc0000b00`;
  - ISO descriptors: `USBD_STATUS_XACT_ERROR = 0xc0000011`.

Вывод:

- Это не порча JPEG и не проблема цвета/кодека. Пока payload доходит, кадры собираются целиком.
- Срыв снова происходит на старте следующего кадра: хост получает только 2-байтный UVC header `0200`, а первый MJPEG payload следующего кадра не оказывается поставлен в то же ISO окно.
- Наиболее подозрительный участок: переход `SOF start -> UVC_STM_ExampleDataIn() -> STREAMING`.

Изменение:

- Оставлен direct start payload path: `uvc_stm_sof_direct_payload_enable = 1`.
- Исправлена ошибка состояния: `SOF` больше не переводит UVC class в `STREAMING` до успешного `USBD_LL_Transmit`.
- Если прямой старт payload не принят, состояние остается `READY`, `uvc_tx_in_flight_dbg = 0`, `uvc_pending_stream_start_dbg = 1`, и следующий `SOF` может повторить нормальную попытку старта.
- `uvc_class_iso_no_active_resync_enable` оставлен выключенным, чтобы проверить именно state-machine fix без дополнительного восстановления.

Что проверить в отладчике после прошивки:

- `uvc_stm_sof_direct_payload_calls`;
- `uvc_stm_sof_direct_payload_ok`;
- `uvc_stm_sof_direct_payload_fail`;
- `uvc_start_payload_fail`;
- `uvc_ll_tx_last_status`;
- `uvc_frames_sent`;
- `pcd_isoin_incomplete_calls`.

Критерий успеха:

- В новом pcap после EOF предыдущего кадра не должно быть одиночного `0200`, после которого идет `data_len = 0 / iso_error_count = 128 / 0xc0000b00`.
- Если `uvc_stm_sof_direct_payload_fail` растет, но поток живет, значит исправление state machine сработало как защита от зависания.

Результат проверки:

- UVC-устройство: `1/49`, `0483:5750`.
- Поток стартовал, затем упал.
- До срыва в pcap видны целые JPEG-кадры размером около `1420..1432` байт.
- Последний нормальный участок:
  - `3.855453 s`: валидный JPEG payload block;
  - `3.871429 s`: header-only `0280,0280`;
  - `3.887438 s`: `data_len = 0`, `USBD_STATUS = 0x00000000`;
  - `3.903432 s`: `data_len = 0`, `USBD_STATUS = 0xc0000b00`;
  - далее идет storm `0xc0010000`.
- Счетчики на плате:
  - `uvc_frames_sent = 71`;
  - `uvc_class_iso_frame_drops ~= 280990`;
  - `uvc_stm_sof_direct_payload_calls ~= 288253`;
  - `uvc_ll_tx_last_status = 0`.

Вывод:

- State-machine fix полезен, но текущая схема все еще рвет DataIn-цепочку в паузах: `UVC_Itf_DataMinimal()` возвращает `psize = 0`, `UVC_STM_ExampleDataIn()` переводит класс в `READY`, и после первой ISO ошибки SOF начинает многократно стартовать payload заново.
- Это объясняет огромные `uvc_stm_sof_direct_payload_calls` и `uvc_class_iso_frame_drops`: мы не один раз восстанавливаем поток, а попадаем в быстрый цикл `SOF start -> IISOIXFR/drop -> SOF start`.

Изменение для следующего прогона:

- В `UVC_Itf_DataMinimal()` в паузах между кадрами, при отсутствии готового нового кадра и сразу после завершения кадра возвращаем `psize = UVC_HEADER_SIZE`, а не `0`.
- Это возвращает STM-like поведение: `DataIn`-цепочка остается живой header-only пакетами, а `SOF` нужен только для начального старта/восстановления, не для каждого пустого интервала.
- Cache/DMA не включались.

Что проверить:

- `uvc_stm_sof_direct_payload_calls` должен перестать убегать в сотни тысяч; нормальный рост должен быть около стартов/восстановлений, а не каждого SOF.
- `uvc_min_header_only_packets` должен расти в паузах.
- Если HAL/LL `O2` теперь успевает обслуживать 2-байтные packets, в pcap после `3.8 s`-подобной границы не должно быть перехода в `0xc0000b00`.

## 2026-04-29: сверка с документацией по isochronous USB и свежим pcap

Свежий `UVC.pcapng` после возврата header-only пакетов:

- UVC-устройство: `1/51`, endpoint `0x81`.
- До срыва идут нормальные payload blocks: в основном `1668/1678` байт и header-only blocks `256` байт.
- Последний data-bearing event перед срывом:
  - `4.973980 s`;
  - `data_len = 66`;
  - `USBD_STATUS = 0`;
  - содержимое фактически состоит из 33 двухбайтовых UVC header-only пакетов.
- Первый плохой event:
  - `4.989977 s`;
  - `data_len = 0`;
  - `IRP USBD_STATUS = USBD_STATUS_ISOCH_REQUEST_FAILED`;
  - `iso_error_count = 128`;
  - все ISO descriptors внутри URB имеют `USBD_STATUS_XACT_ERROR`.
- После этого идут массовые `USBD_STATUS_CANCELED`, то есть приложение/драйвер уже отменяет следующие queued URB после провала transfer.

Вывод по протоколу:

- Isochronous IN не имеет handshake/retry. Если endpoint не ответил корректно в выделенный service interval, этот пакет потерян навсегда.
- При High Speed и `bInterval = 1` endpoint обслуживается каждый microframe, то есть каждые 125 us.
- UVC header-only packet легален как пустой video payload, но он не лечит причину: endpoint все равно обязан быть перевооружен и готов к следующему microframe.
- ZLP и header-only flood уже проверены. Они меняют форму паузы между JPEG кадрами, но не устраняют первичную ошибку `USBD_STATUS_XACT_ERROR`.
- Это больше похоже не на JPEG/цвет/дескриптор FPS, а на одноразовый промах USB endpoint scheduling: firmware в какой-то момент не успевает или неправильно перевооружает isochronous IN endpoint.

Сверка с STM:

- В STM webcam example `SOF` отправляет только первый двухбайтовый packet после `SET_INTERFACE alt=1`.
- Основной поток идет цепочкой `DataIn -> fops.Data() -> USBD_LL_Transmit()`.
- `IsoINIncomplete` в STM example пустой; они не пытаются переигрывать старый payload и не строят recovery-loop.
- STM держит данные кадра уже готовыми для USB; JPEG/camera pipeline не должен блокировать быстрый USB ISR path.

Что это меняет в наших дальнейших действиях:

- Не продолжать лечить паузы разными видами пустых packets.
- Проверить, что `SOF` используется только как первичный старт после `SET_INTERFACE`, а не как постоянный recovery engine после каждого сбоя.
- Упростить быстрый путь: `DataIn` должен только взять уже готовый packet из RAM и вызвать `USBD_LL_Transmit`; без подготовки JPEG, без ожидания pipeline и без сложного восстановления.
- Следующий эксперимент должен быть архитектурным: небольшой prebuilt/ring buffer UVC packets в RAM, который всегда содержит следующий packet для endpoint. Producer кадра может отставать, но USB consumer не должен зависеть от готовности нового JPEG в interrupt path.
- Для диагностики добавить счетчики именно deadline miss: время между `DataIn`, время от входа в `HAL_PCD_IRQHandler` до `USBD_LL_Transmit`, и snapshot endpoint registers в момент первого `USBD_STATUS_XACT_ERROR`/`IISOIXFR`.

## 2026-04-29: свежий pcap после зависания live JPEG, проверка SOF wait-gating

Свежий `UVC.pcapng` после остановки стрима:

- UVC-устройство: `1/52`, endpoint `0x81`.
- До срыва идут нормальные blocks `1668/1678` байт и header-only blocks `256` байт.
- Последний event с `IRP USBD_STATUS = 0` уже не полностью успешный:
  - frame `5555`;
  - `time = 3.492596 s`;
  - `data_len = 56`;
  - внутри URB `iso_error_count = 100`;
  - фактически дошли только 28 двухбайтовых header-only packets, а остальные microframes в этом же URB уже имеют `USBD_STATUS_XACT_ERROR`.
- Следующий event:
  - frame `5571`;
  - `time = 3.508596 s`;
  - `data_len = 0`;
  - `IRP USBD_STATUS = USBD_STATUS_ISOCH_REQUEST_FAILED`;
  - `iso_error_count = 128`.

Вывод:

- Endpoint перестает отвечать не на границе JPEG и не из-за битого JPEG, а посреди обычной серии header-only packets.
- Это почти напрямую указывает на состояние, где firmware перестал перевооружать EP1 IN, пока хост продолжал опрашивать isochronous endpoint каждый service interval.
- Наиболее подозрительный путь в коде: `IsoINIncomplete` при no-active-frame переводит класс в `READY`, после чего `SOF` мог попасть в `backend_state.wait_frame_interval` или `wait pending` и выйти без `USBD_LL_Transmit`.

Изменение для проверки:

- В `USBD_UVC_SOF()` убраны ранние выходы:
  - ожидание `backend_state.wait_frame_interval` с редкими keepalive packets;
  - ожидание pending frame, когда `video_source_can_accept_frame()` и нет текущего кадра.
- Теперь при активном `alt=1` и состоянии `READY` `SOF` всегда пытается запустить ту же STM-like цепочку через `UVC_STM_ExampleDataIn()`.
- Решение, что отправлять в паузе между кадрами, остается внутри `UVC_Itf_DataMinimal()`: если JPEG еще не надо/не готов, она возвращает `psize = UVC_HEADER_SIZE`, то есть обычный 2-байтный UVC header-only packet.
- Сборка после изменения успешна. Cache/DMA не включались.

Критерий проверки:

- После нового зависания в pcap не должно быть картины `несколько header-only packets -> внутри того же URB десятки XACT_ERROR -> 0xc0000b00`.
- `uvc_stm_sof_wait_pending_calls` больше не должен расти.
- Если поток все еще сорвется, следующая точка поиска: почему `USBD_LL_Transmit()` перестал вызываться/успевать именно внутри `DataIn`-цепочки, а не в SOF wait path.

## 2026-04-29: переход на явную UVC runtime state machine

Сделано после решения откатить старые ad-hoc recovery/replay/drop попытки из активного пути и собрать поведение вокруг одного места, где endpoint ставится на передачу.

Новая модель:

- Единственная функция постановки transfer: `UVC_PrimeNextPacket()`.
- Runtime-состояние:
  - `streaming_enabled`;
  - `ep_busy`;
  - `drop_current_frame`;
  - `fid`;
  - `last_packet_was_eof`;
  - текущий `frame_ptr/frame_size/offset`;
  - `next_frame_tick` для frame pacing.
- USB TX идет через отдельный 32-byte aligned packet buffer `uvc_runtime_tx_packet`.
- Большой JPEG целиком в ISR не копируется: копируется только один UVC packet до `UVC_IN_PACKET`.
- DCache не включается. Если DCache уже включен извне, перед IN transfer выполняется clean только для USB TX packet buffer.

Callbacks:

- `SET_INTERFACE alt=1`:
  - включает `streaming_enabled`;
  - сбрасывает `ep_busy/drop_current_frame`;
  - flush endpoint;
  - стартует producer через `Start()`;
  - первый packet ставит ближайший `SOF`.
- `SET_INTERFACE alt=0`:
  - выключает `streaming_enabled`;
  - сбрасывает runtime state;
  - останавливает producer;
  - flush endpoint.
- `SOF`:
  - теперь только watchdog/start trigger;
  - если stream включен и `ep_busy == false`, вызывает `UVC_PrimeNextPacket()`.
- `DataIn`:
  - сбрасывает `ep_busy`;
  - если завершился EOF packet, закрывает кадр и переключает FID для следующего кадра;
  - сразу вызывает `UVC_PrimeNextPacket()`.
- `IsoINIncomplete`:
  - сбрасывает `ep_busy`;
  - если был активный кадр, дропает его и переключает FID;
  - сразу вызывает `UVC_PrimeNextPacket()`;
  - старые replay/retry ветки больше не участвуют в callback path.
- `USBD_UVC_WatchdogPoll()`:
  - больше не вызывает старый `UVC_ResetBackendForRestart()`;
  - если внешний timeout включат через `uvc_tx_watchdog_timeout_ms`, он сбросит `ep_busy`, дропнет активный кадр и также пойдет через `UVC_PrimeNextPacket()`.

UVC payload header:

- `byte0 = 0x02`.
- `byte1 = 0x80 | (fid & 1)`.
- `EOF` (`0x02`) ставится только на последнем payload кадра.
- `FID` меняется только после успешного EOF или после drop поврежденного кадра.

Новые counters для отладчика:

- Смотреть структуру `uvc_runtime_dbg`.
- Минимально важные поля при зависании:
  - `streaming_enabled`;
  - `ep_busy`;
  - `drop_current_frame`;
  - `cnt_sof`;
  - `cnt_data_in`;
  - `cnt_iso_in_incomplete`;
  - `cnt_transmit_fail`;
  - `cnt_underrun`;
  - `cnt_dropped_frames`;
  - `cnt_eof`;
  - `cnt_prime_calls`;
  - `cnt_prime_ok`;
  - `cnt_header_only`;
  - `cnt_payload`;
  - `last_status`;
  - `last_len`;
  - `last_header`;
  - `last_offset`;
  - `last_frame_size`.

Ожидание от проверки:

- `ep_busy` не должен оставаться `1` навсегда: `DataIn` и `IsoINIncomplete` оба сбрасывают его.
- После producer underrun должны расти `cnt_header_only` и/или `cnt_underrun`, но поток не должен останавливаться.
- После `IsoINIncomplete` должен расти `cnt_iso_in_incomplete`, затем `cnt_dropped_frames` при активном кадре и `cnt_prime_ok`, если endpoint удалось снова поставить.
- Если stream зависнет при `ep_busy = 0`, значит `SOF` watchdog не приходит или `UVC_PrimeNextPacket()` не вызывается.
- Если stream зависнет при `ep_busy = 1`, значит нет ни `DataIn`, ни `IsoINIncomplete`; тогда смотреть нижний PCD/HAL interrupt path и endpoint regs.

## 2026-04-29: pcap после runtime state machine - обнаружен stuck `ep_busy`

Свежий `UVC.pcapng`:

- UVC-устройство: `1/59`, endpoint `0x81`.
- До срыва поток идет регулярным паттерном:
  - payload blocks `1668/1678`;
  - header-only blocks `256`.
- Последняя нормальная последовательность:
  - `4.668631 s`: `data_len = 1668`, `iso_error_count = 0`;
  - `4.684637 s`: `data_len = 256`, `iso_error_count = 0`;
  - `4.700631 s`: `data_len = 90`, `IRP_STATUS = SUCCESS`, но `iso_error_count = 83`.
- В event `30625` фактически дошли 45 packets `02 81`, затем остальные 83 microframes имеют `USBD_STATUS_XACT_ERROR`.
- Следующий URB `30845` уже полностью плохой:
  - `data_len = 0`;
  - `IRP_STATUS = USBD_STATUS_ISOCH_REQUEST_FAILED`;
  - `iso_error_count = 128`.
- Control-запросов `SET_INTERFACE` в момент срыва нет. Следующий control на `1.59.0` появляется только около `7.980 s`, то есть уже после остановки.

Состояние платы со скрина после срыва:

- `streaming_enabled = 1`;
- `ep_busy = 1`;
- `last_status = 0`;
- `last_len = 512`;
- `last_header = 129` (`0x81`, EOH + FID);
- `cnt_transmit_fail = 0`;
- `cnt_iso_in_incomplete` очень большой.

Вывод:

- Host не остановил stream управляющим запросом.
- JPEG не является непосредственной причиной этого срыва: первый сбой происходит в серии header-only packets.
- Классическая цепочка recovery работает много раз, но в финале остается состояние `ep_busy = 1`: firmware считает, что transfer успешно поставлен, но больше не получает ни `DataIn`, ни `IsoINIncomplete`.
- Значит нужен watchdog именно на stuck `ep_busy`, а не только recovery внутри `IsoINIncomplete`.

Изменение для проверки:

- Добавлен `uvc_runtime_busy_timeout_ms = 2`.
- В `SOF`, если `streaming_enabled == 1`, `ep_busy == 1`, а с момента успешного `USBD_LL_Transmit` прошло больше timeout:
  - увеличивается `uvc_runtime_dbg.cnt_busy_timeout`;
  - сохраняются `busy_diepctl/busy_dieptsiz/busy_diepint`;
  - выполняется `USBD_LL_FlushEP`;
  - `ep_busy` сбрасывается;
  - активный кадр дропается;
  - вызывается `UVC_PrimeNextPacket()`.

Что смотреть в следующем прогоне:

- `uvc_runtime_dbg.cnt_busy_timeout`.
- `uvc_runtime_dbg.busy_diepctl`, особенно бит `EPENA`.
- `uvc_runtime_dbg.busy_dieptsiz`.
- Если после busy-timeout поток восстановится, причина была в потерянном callback.
- Если `cnt_busy_timeout` растет, но pcap все равно остается в `XACT_ERROR`, значит endpoint физически/регистрово остается в плохом состоянии и следующий шаг - аккуратный EP disable/reopen или низкоуровневая проверка `DIEPCTL` frame parity.

## 2026-04-29: гипотеза iliasam - очистка Tx FIFO перед записью в endpoint

Источник идеи: в статье iliasam по `STM32F4_UVC_Camera` описан похожий дефект STM USB library: после небольшого объема передачи endpoint перестает отдавать данные хосту, а исправление состояло в предварительной очистке FIFO нужной конечной точки перед началом записи.

Почему это похоже на нашу картину:

- В pcap срыв происходит не внутри JPEG, а в серии коротких/header-only или обычных UVC IN передач.
- `USBD_LL_Transmit()` возвращает `USBD_OK`, `last_status = 0`, но затем endpoint перестает отвечать хосту: `XACT_ERROR`, потом `USBD_STATUS_ISOCH_REQUEST_FAILED`.
- В отладчике после срыва видно `ep_busy = 1`, то есть firmware считает transfer поставленным, но не получает ни `DataIn`, ни полезного восстановления через `IsoINIncomplete`.

Изменение для проверки:

- Добавлен переключатель `uvc_runtime_flush_before_tx_enable = 1`.
- В единственной активной точке постановки UVC packet, `UVC_PrimeNextPacket()`, перед `USBD_LL_Transmit()` вызывается `USBD_LL_FlushEP(pdev, UVC_IN_EP)`.
- Очистка выполняется только после проверки `ep_busy == 0`, то есть не поверх логически активного transfer.
- Добавлены счетчики в `uvc_runtime_dbg`:
  - `cnt_flush_before_tx`;
  - `cnt_flush_before_tx_fail`;
  - `last_flush_status`.

Что смотреть после прошивки:

- Если гипотеза верна, в pcap должны исчезнуть или резко сократиться серии `XACT_ERROR -> 0xc0000b00`, а поток должен переживать длинные паузы/header-only участки.
- `cnt_flush_before_tx` должен расти примерно вместе с `cnt_prime_ok`.
- `cnt_flush_before_tx_fail` должен оставаться `0`.
- Если поток все равно остановится, важны: `ep_busy`, `cnt_busy_timeout`, `last_flush_status`, `busy_diepctl`, `busy_dieptsiz`, `busy_diepint`.

Риск/ограничение:

- HAL flush Tx FIFO сам по себе должен выполняться только когда USB core не читает и не пишет FIFO. Мы вызываем его в наиболее безопасной для текущей архитектуры точке - перед новой постановкой transfer, когда `ep_busy == 0`. Если появятся новые проблемы с перечислением или стабильностью, первым делом выключить эксперимент через `uvc_runtime_flush_before_tx_enable = 0`.

Результат pcap после проверки:

- UVC endpoint: `1.63.1`, EP `0x81`.
- До срыва идут нормальные UVC URB blocks с `data_len = 1668/1678` и header-only blocks `data_len = 256`.
- Последний плохой URB:
  - frame `4651`;
  - time `4.353647 s`;
  - `IRP_STATUS = SUCCESS`;
  - `data_len = 26`;
  - `iso_error_count = 115`.
- Внутри этого URB дошли только 13 коротких packets `02 81`, затем 115 микрофреймов подряд получили `USBD_STATUS_XACT_ERROR`.
- После этого поток не восстановился до `SET_INTERFACE alt=0` примерно на `10.766765 s`.
- Счетчики платы:
  - `cnt_flush_before_tx = 136117`;
  - `cnt_flush_before_tx_fail = 0`;
  - `last_flush_status = 0`;
  - `ep_busy = 0`;
  - `cnt_busy_timeout = 0`.

Вывод:

- Гипотеза FIFO не опровергнута полностью, но очистка перед каждым `USBD_LL_Transmit()` сама по себе не убрала срыв.
- Срыв выглядит как пропущенный deadline/re-arm ISO IN endpoint: часть микрофреймов успела получить header-only packet, затем endpoint перестал отвечать в оставшейся части host URB.
- Это не похоже на порчу JPEG: плохой участок содержит только header-only packets.

Следующий эксперимент:

- Уменьшить частоту опроса HS isochronous endpoint: `UVC_HS_EP_INTERVAL = 3`.
- Это означает service interval `2^(3-1) * 125 us = 500 us`.
- При `UVC_IN_PACKET = 512` доступная полезная скорость остается около `1 MB/s`, что выше текущего `dwMaxBitRate` для `UVC_MAX_FRAME_SIZE = 8192` и `30 fps`.
- Цель: проверить, является ли первопричиной слишком жесткий 125-us deadline при включенных SDRAM/LTDC/JPEG/MDMA.

Что смотреть после прошивки:

- В pcap endpoint descriptor должен показывать `bInterval = 3`.
- Нормальные UVC blocks должны идти реже, но без длинной серии `XACT_ERROR` после коротких `02 81`.
- В отладчике важно сравнить:
  - `cnt_data_in`;
  - `cnt_iso_in_incomplete`;
  - `cnt_underrun`;
  - `cnt_dropped_frames`;
  - `cnt_flush_before_tx_fail`;
  - `cnt_busy_timeout`.

Результат проверки `UVC_HS_EP_INTERVAL = 3`:

- На экране stream не начался.
- По счетчикам:
  - `streaming_enabled = 1`;
  - `ep_busy = 1`;
  - `cnt_data_in = 4013`;
  - `cnt_iso_in_incomplete = 39345`;
  - `cnt_underrun = 39296`;
  - `cnt_dropped_frames = 39345`;
  - `cnt_eof = 0`;
  - `cnt_payload = 43359`;
  - `cnt_header_only = 0`;
  - `last_len = 512`;
  - `last_header = 129`;
  - `last_offset = 510`;
  - `last_frame_size = 1422`.
- Это означает, что прошивка постоянно ставила payload, но почти каждый кадр сбрасывался до EOF. Хост не получал ни одного завершенного UVC frame.
- Файл `UVC.pcapng` на диске при проверке имел старый timestamp, поэтому новый pcap в этот пункт не попал.

Вывод:

- Для текущего STM HAL/OTG HS isochronous path `bInterval = 3` не подходит. Вероятно, HAL-логика `DIEPCTL.SODDFRM/SD0PID_SEVNFRM` и наш re-arm path рассчитаны на опрос каждый микрофрейм; при более редком service interval endpoint почти всегда уходит в `IsoINIncomplete`.
- Откат: `UVC_HS_EP_INTERVAL = 1`.

Дополнительная проверка pcap после сообщения "обновил pcap":

- Файл `C:\Users\Professional\Documents\UVC.pcapng`: timestamp `2026-04-29 18:36:45`, размер `8484156`.
- В pcap UVC-устройство `1.7` по-прежнему отдает endpoint descriptor:
  - `wMaxPacketSize = 512`;
  - `bInterval = 3`.
- Хост делает `SET_INTERFACE alt=1` на `1.703018 s`.
- После этого на `1.772542 s` проходит один ISO packet `512` байт, затем дальше в основном идут ISO URB с `Packet Data Length = 0`.
- Локальный исходник уже возвращен на `UVC_HS_EP_INTERVAL = 1`, и `Debug\BaseH743.elf` пересобран после отката.

Вывод:

- Захват сделан не с прошивки после отката, или CubeIDE/отладчик прошил другой ELF.
- Для проверки следующего шага сначала нужно убедиться, что в отладчике `uvc_desc_ep_interval_dbg == 1`, а в pcap configuration descriptor показывает `bInterval: 1`.
## 2026-04-29: проверка свежего pcap после вопроса про JPEG/FID

Файл `C:\Users\Professional\Documents\UVC.pcapng`, timestamp `2026-04-29 18:45:09`.

Что видно в захвате:

- UVC device `1.11`, EP `0x81`.
- Дескриптор согласован с текущим исходником:
  - `wMaxPacketSize = 512`;
  - `bInterval = 1`;
  - `dwMaxPayloadTransferSize = 512`;
  - MJPEG `160 x 120`;
  - `dwMaxVideoFrameBufferSize = 8192`;
  - `dwDefaultFrameInterval = 333333` 100ns units.
- В ISO payload все UVC headers начинаются с `0x02`.
- Завершенные JPEG кадры в pcap выглядят корректно:
  - 148 EOF-событий;
  - 147 полных кадров с `FF D8 ... FF D9`;
  - первый неполный кадр объясняется тем, что захват начался в середине текущего кадра.
- FID в pcap не переключается внутри кадра без EOF.
- Последний нормальный кадр завершился около `7.418993 s`: пакет с EOF `02 82 ... FF D9`, после него в том же URB еще идут header-only пакеты уже с новым FID `02 81`.
- После этого host продолжает ISO polling, но полезные данные больше не приходят: идут zero-length ISO completions до `SET_INTERFACE alt=0`.

Вывод:

- В этом захвате нет признаков, что JPEG producer отдал испорченный кадр.
- Нет признаков ошибки FID внутри кадра.
- Срыв выглядит как прекращение re-arm/prime IN endpoint после корректного EOF/header-only, а не как повреждение JPEG payload.

Изменение для проверки:

- Добавлена защита `video_source_submit_frame()` от не-MJPEG буфера: pending frame принимается только если кадр начинается с `FF D8` и заканчивается `FF D9`.
- Добавлены отладочные счетчики:
  - `dbg_submit_reject_bad_jpeg`;
  - `dbg_submit_last_size`;
  - `dbg_submit_head`;
  - `dbg_submit_tail`;
  - `uvc_runtime_dbg.cnt_bad_jpeg_frame`;
  - `uvc_runtime_dbg.cnt_frame_load`;
  - `uvc_runtime_dbg.cnt_fid_toggle_eof`;
  - `uvc_runtime_dbg.cnt_fid_toggle_drop`.

Что смотреть в следующем прогоне:

- Если `dbg_submit_reject_bad_jpeg` или `uvc_runtime_dbg.cnt_bad_jpeg_frame` растут, проблема действительно может быть в JPEG/buffer ownership.
- Если они остаются `0`, а поток снова останавливается после корректных EOF, фокус остается на USB endpoint re-arm / DataIn-IsoINIncomplete-SOF state machine.
## 2026-04-29: свежий pcap `19:07:07`, поток останавливается без порчи JPEG

Файл `C:\Users\Professional\Documents\UVC.pcapng`, timestamp `2026-04-29 19:07:07`, длительность около `5.716 s`.

Что видно:

- UVC device в этом захвате: `1.26`, EP `0x81`.
- Дескриптор снова согласован:
  - `wMaxPacketSize = 512`;
  - `bInterval = 1`;
  - `dwMaxPayloadTransferSize = 512`;
  - MJPEG `160 x 120`;
  - `dwMaxVideoFrameBufferSize = 8192`;
  - `dwDefaultFrameInterval = 333333`.
- `SET_INTERFACE alt=1` один раз, около `1.188189 s`.
- Полезные UVC данные идут примерно с `1.209302 s` до `3.001301 s`.
- За это время восстановлено:
  - `54` завершенных кадра;
  - `54/54` валидные JPEG `FF D8 ... FF D9`;
  - `0` плохих UVC headers;
  - `0` FID toggle без EOF;
  - размеры кадров `1412/1422` байта;
  - каждый кадр занимает `3` payload packets.
- Объем UVC microtransactions до остановки:
  - payload packets: `162`;
  - header-only packets: `14288`.
- Последний полезный URB frame `2417`, time `3.001301 s`, содержит корректный EOF `02 83 ... FF D9`, после него несколько header-only `02 80`.
- Сразу после этого, начиная примерно с frame `2423`, host продолжает ISO polling, но от устройства идут только zero-length ISO completions. До конца захвата новых `02 xx` UVC headers больше нет.
- В этом pcap явных `XACT_ERROR` не видно; картина не "пакет потерялся внутри кадра", а "endpoint больше не получает/не отдает поставленные данные".

Вывод:

- JPEG producer и FID/EOF снова не выглядят первопричиной.
- Поток умирает после нормального кадра, в фазе header-only/ожидания следующего кадра.
- Очень подозрительна текущая стратегия непрерывно слать 2-байтовые header-only packets каждый 125 us между маленькими JPEG кадрами: на 54 кадра получилось `14288` пустых пакетов против `162` полезных.

Следующий логичный эксперимент:

- Не спамить header-only каждый microframe во время ожидания `next_frame_tick`.
- Если кадр еще не пора/не готов, оставить endpoint idle до следующего SOF/main-poll, а при готовом кадре сразу prime payload.
- Цель: проверить, исчезнет ли остановка после резкого уменьшения количества пустых ISO IN transfers.
## 2026-04-29: добавлена диагностика точки остановки state machine

Цель изменения: после следующего зависания отличить три случая:

- `SOF` больше не приходит в UVC class path.
- `SOF` приходит, но `PrimeNextPacket()` не вызывается или выходит рано.
- `USBD_LL_Transmit()` успешно вызывается, но на шине больше нет `02 xx` UVC packets.

Добавлены поля в `uvc_runtime_dbg`:

- `current_alt_setting` - текущий alternate setting VS interface.
- `huvc_state` - состояние UVC class handle.
- `frame_active` - активен ли сейчас кадр в runtime state.
- `cnt_sof_idle_prime` - сколько раз SOF видел `ep_busy == 0` и пытался prime.
- `cnt_sof_busy` - сколько раз SOF видел `ep_busy != 0`.
- `cnt_data_in_bad_ep` - DataIn пришел не для EP1.
- `cnt_iso_in_bad_ep` - IsoINIncomplete пришел не для EP1.
- `cnt_prime_skip_no_handle` - `UVC_PrimeNextPacket()` вышел из-за отсутствия handle/pdev.
- `cnt_prime_skip_not_streaming` - выход из-за `streaming_enabled == 0`.
- `cnt_prime_skip_alt` - выход из-за `current_alt_setting != 1`.
- `cnt_prime_skip_busy` - выход из-за `ep_busy != 0`.
- `last_prime_reason`:
  - `0` none;
  - `1` no handle;
  - `2` not streaming;
  - `3` wrong alt setting;
  - `4` EP busy;
  - `5` payload packet queued;
  - `6` header-only packet queued;
  - `7` transmit failed.

Как читать результат после зависания:

- `cnt_sof` растет, `cnt_sof_busy` растет, `ep_busy = 1`: завис последний IN transfer / потерян completion.
- `cnt_sof` растет, `cnt_sof_idle_prime` растет, `cnt_prime_ok` растет, но в pcap нет `02 xx`: низкоуровневый клин EP/FIFO/OTG, firmware считает transfer поставленным.
- `cnt_prime_skip_alt` растет или `current_alt_setting != 1`: class state ошибочно ушел из streaming alt setting.
- `cnt_prime_skip_not_streaming` растет или `streaming_enabled = 0`: runtime ошибочно выключил streaming.
- `cnt_data_in_bad_ep` или `cnt_iso_in_bad_ep` растет: снова проблема маршрутизации callback endpoint number.
- `cnt_sof` не растет: проблема выше, в USB IRQ/SOF dispatch.

## 2026-04-29: pcap `19:53`, срыв после успешных `USBD_LL_Transmit`

Новый pcap и `uvc_runtime_dbg` после остановки потока:

- `streaming_enabled = 1`, `current_alt_setting = 1`, `huvc_state = 2`.
- `ep_busy = 0`: UVC runtime не застрял в busy.
- Все skip-счетчики `UVC_PrimeNextPacket()` равны нулю.
- `cnt_prime_ok = 90781`, `cnt_transmit_fail = 0`, `last_status = 0`.
- `last_prime_reason = 5`, `last_len = 512`, `last_header = 0x80`, `last_offset = 0`: последний успешно поставленный transfer был настоящим payload, а не header-only.
- `cnt_iso_in_incomplete = 26735`, `cnt_underrun = 26757`, `cnt_dropped_frames = 26595`: после срыва код почти всегда ставит первый payload кадра, получает incomplete, дропает кадр и начинает следующий.

Что видно на шине:

- До `9.55 s` идут корректные MJPEG кадры `1412` байт, по `3` payload packets.
- После последнего нормального кадра host продолжает ISO IN polling, но получает `USBD_STATUS_ISOCH_REQUEST_FAILED`.
- Внутри URB: `error count = 128`, все ISO descriptors имеют `USBD_STATUS_XACT_ERROR`, длина данных `0`.
- После этого новых `02 xx` UVC payload/header packets в pcap больше нет, хотя firmware продолжает получать `USBD_OK` от `USBD_LL_Transmit()`.

Вывод:

- Это уже не JPEG, не FID/EOF и не верхняя UVC state machine.
- Дескрипторы и FIFO-сайзы на этом этапе согласованы со STM: EP1 Tx FIFO `0x300`, packet size `512`, `dwMaxPayloadTransferSize = 512`.
- Самый вероятный слой проблемы: низкоуровневое расписание HS isoch IN endpoint / Tx FIFO / frame parity. Мы можем успешно записать request в HAL, но endpoint не отвечает на IN token.

Изменение для следующей проверки:

- После неудачного эксперимента с зеленой картинкой оба низкоуровневых переключателя возвращены к предыдущему состоянию:
  - `uvc_runtime_flush_before_tx_enable = 1`;
  - `uvc_isoin_incomplete_handling_enable = 1`.

Что проверить после прошивки:

- Если поток станет стабильнее или `cnt_iso_in_incomplete` резко уменьшится, причина была в одном из наших низкоуровневых вмешательств: per-packet FIFO flush или ручной frame parity resync.
- Если картина не изменится, следующий эксперимент: уменьшить header-only spam между кадрами и не arm-ить endpoint до готовности следующего payload.

## 2026-04-29: зеленая картинка после выключения ручного resync

Эксперимент `uvc_isoin_incomplete_handling_enable = 0` оказался неудачным.

Симптомы в отладчике:

- `cnt_eof = 1`, при этом `cnt_payload = 281538`.
- `cnt_iso_in_incomplete = 281913`, `cnt_underrun = 281160`, `cnt_dropped_frames = 281514`.
- `cnt_fid_toggle_drop = 281521`.
- `cnt_bad_jpeg_frame = 0`.

Что видно в pcap:

- UVC device: `0483:5750`, адрес `1.50`, EP `0x81`.
- После `SET_INTERFACE alt=1` проходит один крупный ISO URB с данными, но уже с `iso_error_count = 66`.
- Внутри этого URB несколько payload packets подряд начинаются с `02 81 FF D8...`, `02 80 FF D8...`, то есть каждый раз снова отправляется начало JPEG, FID при этом переключается.
- Затем идут URB с `USBD_STATUS_ISOCH_REQUEST_FAILED`, `error_count = 128`, все ISO descriptors `USBD_STATUS_XACT_ERROR`.

Вывод:

- Зеленая картинка не связана с DMA2D/цветом кадра. Host получает не нормальный MJPEG frame, а повторяющиеся начала JPEG без корректного EOF.
- Выключение ручного odd/even resync ломает восстановление после `IsoINIncomplete`: class постоянно дропает текущий кадр, переключает FID и начинает следующий с offset `0`, поэтому до хоста доходят обрывки.

Решение:

- Вернуть полный предыдущий baseline:
  - `uvc_isoin_incomplete_handling_enable = 1`;
  - `uvc_runtime_flush_before_tx_enable = 1`.

## 2026-04-29 20:28: зависание после валидных кадров, endpoint/core перестает отвечать

Свежий `UVC.pcapng` после возврата baseline:

- Устройство: `0483:5750`, USB address `58`, stream EP `0x81`.
- `SET_INTERFACE alt=1`: `1.261517 s`, успешно.
- До `2.882965 s` идут нормальные UVC/MJPEG пакеты:
  - header-only packets `02 80` / `02 81`;
  - MJPEG payload packets с `FF D8`;
  - EOF packets с UVC header `02 82` / `02 83`.
- Первый фатальный сбой: frame `3415`, `2.898963 s`.
  - `IRP USBD_STATUS = USBD_STATUS_ISOCH_REQUEST_FAILED (0xc0000b00)`;
  - `128/128` ISO packets имеют `USBD_STATUS_XACT_ERROR (0xc0000011)`;
  - длина всех ISO packets `0`.
- После этого полезных данных по EP `0x81` больше нет.
- При закрытии приложения Camera `SET_INTERFACE alt=0` по EP0 тоже завершается `USBD_STATUS_XACT_ERROR`.

Counters в момент зависания:

- `streaming_enabled = 1`, `current_alt_setting = 1`, `huvc_state = 2`.
- `ep_busy = 0`: class state не завис в busy.
- `cnt_prime_ok = 45554`, `cnt_transmit_fail = 0`, `last_status = 0`.
- `cnt_eof = 48`: до срыва host успел получить целые кадры.
- `cnt_iso_in_incomplete = 32705`, `cnt_underrun = 32500`, `cnt_dropped_frames = 32501`.
- `cnt_flush_before_tx = 45554`: текущая сборка делает `USBD_LL_FlushEP()` перед каждой постановкой EP1 transfer.

Вывод:

- Корень текущего зависания ниже JPEG producer и ниже UVC header/FID/EOF.
- Это не выглядит как несогласованность fps/bitrate/descriptors: stream успевает передать несколько десятков корректных кадров, а затем endpoint внезапно получает полный URB `XACT_ERROR`.
- Это не выглядит как нехватка готового кадра: даже после срыва firmware продолжает получать `USBD_OK` от `USBD_LL_Transmit()`, но на шине новых `02 xx` packets больше нет.
- Самый подозрительный нестоковый элемент сейчас: flush EP1 FIFO перед каждым `USBD_LL_Transmit()`. Он был добавлен как лечение по мотивам чужого опыта, но на этой сборке выполняется десятки тысяч раз, включая header-only packets.

Следующий эксперимент, чтобы не ходить кругами:

1. Не менять JPEG, descriptors, fps и frame size.
2. Оставить включенными подтвержденные важные части:
   - `IISOIXFR` EP0 -> EP1 remap;
   - ручной odd/even frame resync;
   - drop текущего MJPEG frame при реальном `IsoINIncomplete`.
3. Ввести управляемую политику flush:
   - `0`: без flush перед normal TX;
   - `1`: текущий режим, flush перед каждым TX;
   - `2`: flush только на recovery (`SET_INTERFACE alt=1`, `IsoINIncomplete`, busy timeout), но не перед каждым нормальным payload/header-only TX.
4. Добавить диагностику:
   - сколько раз flush выполнялся при установленном `DIEPCTL.EPENA`;
   - `DIEPCTL/DIEPTSIZ/DIEPINT` до и после flush;
   - длину header-only streak между payload frames.
5. Проверить pcap в режиме `flush_policy = 2`.

Ожидаемая развилка:

- Если `flush_policy = 2` уберет полный `128/128 XACT_ERROR`, значит мы сами клиним/ломаем EP1 чрезмерной очисткой FIFO.
- Если не изменит картину, следующий подозреваемый - слишком агрессивный header-only поток между маленькими JPEG; тогда проверять режим, где endpoint не arm-ится бесконечными 2-байтными packetами, а повторяет последний полный кадр или ждет ближайшего настоящего payload.

## 2026-04-29 20:42: повторный pcap перед `flush_policy = 2`

Свежий pcap подтвердил ту же причину, но дал более точную точку начала:

- Устройство: `0483:5750`, USB address `57`, EP `0x81`.
- Последний большой payload перед срывом: frame `21248`, `2.075773 s`, `1678` байт.
- Следующий EP1 URB: frame `21258`, `2.091769 s`, `224` байта header-only данных `02 81`, но уже с `16` ISO descriptors `USBD_STATUS_XACT_ERROR`.
- Следующий EP1 URB: frame `21528`, `2.107769 s`, `USBD_STATUS_ISOCH_REQUEST_FAILED`, `128/128` ISO descriptors `XACT_ERROR`, длина `0`.

Counters:

- `ep_busy = 1`: последний IN transfer не получил нормальный completion.
- `cnt_eof = 18`: до срыва были целые кадры.
- `cnt_flush_before_tx = 91813`: per-packet flush снова совпадает с числом успешных постановок transfer.
- `cnt_header_only = 4841`: перед срывом есть заметный поток коротких header-only packets.

Уточненный вывод:

- Первый видимый на хосте сбой появляется в header-only окне между кадрами, а не на JPEG payload.
- Поэтому следующий эксперимент не должен менять JPEG, размер кадра, FID/EOF или descriptors.
- Проверяем одну вещь: не провоцирует ли клин EP1 постоянный `USBD_LL_FlushEP()` перед каждым 2-байтным или payload packet.

Изменение в коде:

- Добавлен `uvc_runtime_flush_policy`:
  - `0`: без flush;
  - `1`: flush перед каждым TX, старое поведение;
  - `2`: recovery-only flush, новое поведение по умолчанию.
- `uvc_runtime_flush_before_tx_enable = 0` по умолчанию.
- `uvc_runtime_flush_policy = 2` по умолчанию.
- `USBD_LL_FlushEP()` теперь вызывается через `UVC_FlushStreamEP()`:
  - при `SET_INTERFACE alt0/alt1`;
  - при `IsoINIncomplete`;
  - при busy-timeout;
  - перед каждым TX только если `uvc_runtime_flush_policy == 1` или вручную включен старый `uvc_runtime_flush_before_tx_enable`.
- Добавлены диагностические поля:
  - `cnt_flush_recovery`;
  - `cnt_flush_while_epena`;
  - `last_flush_reason`;
  - `flush_before_diepctl`, `flush_before_dieptsiz`, `flush_before_diepint`;
  - `flush_after_diepctl`, `flush_after_dieptsiz`, `flush_after_diepint`.

Что смотреть после следующего запуска:

- `cnt_flush_before_tx` должен остаться около `0` при штатной передаче.
- `cnt_flush_recovery` должен расти только при alt switch / incomplete / busy recovery.
- Если stream стабилизируется или первый `XACT_ERROR` исчезнет, причина была в чрезмерной очистке FIFO перед normal TX.
- Если stream снова падает в header-only окне, следующий эксперимент: уменьшить/отключить бесконечную серию header-only packets между кадрами.

## 2026-04-29 20:54: `flush_policy = 2` не убрал первый header-only XACT

Результат проверки:

- `cnt_flush_before_tx = 0`: per-TX flush действительно отключен.
- `cnt_flush_recovery = 36046`.
- `cnt_flush_while_epena = 36043`, практически равно `cnt_iso_in_incomplete = 36043`.
- Последний flush был из ISO recovery: `last_flush_reason = 3`.
- Перед flush регистры EP1:
  - `DIEPCTL = 0x80448200`: `EPENA` установлен, endpoint активен;
  - `DIEPTSIZ = 0x20080000`;
  - `DIEPINT = 0x00000080`.

Что видно в pcap:

- До `2.876699 s` идут нормальные MJPEG payload URB.
- На `2.892669 s` header-only URB имеет `156` байт данных `02 81`, но `50` ISO descriptors уже `XACT_ERROR`.
- На `2.908665 s` следующий URB полностью `128/128 XACT_ERROR`.

Вывод:

- Гипотеза "главная причина только в flush перед каждым normal TX" ослаблена.
- Срыв все еще начинается в header-only окне, то есть JPEG payload и FID/EOF по-прежнему не выглядят корнем.
- Новый подозреваемый: `FlushEP` внутри `IsoINIncomplete` при еще активном EP (`EPENA=1`). Такой flush выполнялся десятки тысяч раз и может мешать восстановлению вместо помощи.

Изменение:

- Добавлен `uvc_runtime_flush_on_iso_enable`.
- Значение по умолчанию: `0`.
- В `USBD_UVC_IsoINIncomplete()` flush теперь выполняется только если:
  - `uvc_runtime_flush_policy == UVC_FLUSH_POLICY_RECOVERY`;
  - и `uvc_runtime_flush_on_iso_enable != 0`.

Следующая проверка:

- `cnt_flush_before_tx` должен быть `0`.
- `cnt_flush_recovery` должен расти только на `SET_INTERFACE` и busy-timeout, но не на каждый `IsoINIncomplete`.
- `cnt_flush_while_epena` должен перестать расти вместе с `cnt_iso_in_incomplete`.
- Если срыв останется тем же, следующий шаг - проверять саму стратегию header-only packets между кадрами: ограничить их количество или не arm-ить EP1 бесконечными 2-байтными пакетами.

## 2026-04-29 20:58: отключение ISO flush признано неудачным

Результат проверки `uvc_runtime_flush_on_iso_enable = 0`:

- Stream визуально не стартует нормально, экран зеленый.
- `cnt_eof = 1`, `cnt_data_in = 25`.
- `cnt_iso_in_incomplete = 149990`, `cnt_dropped_frames = 149754`.
- `cnt_flush_before_tx = 0`, `cnt_flush_recovery = 3`, `cnt_flush_while_epena = 0`.

Что видно в pcap:

- Первый и единственный ненулевой UVC URB: frame `917`, `1.040000 s`.
- URB формально `SUCCESS`, но `103/128` ISO descriptors уже `XACT_ERROR`.
- Данные начинаются не с UVC header `02 xx`, а с хвоста JPEG (`01 ca 1f 63`, затем `ff d9`).
- Следующий URB уже полностью `128/128 XACT_ERROR`.

Вывод:

- Отключать flush/resync из `IsoINIncomplete` нельзя: без него поток может начать отдавать хвост/середину кадра, что объясняет зеленую картинку.
- Рабочее состояние для дальнейших проверок:
  - `uvc_runtime_flush_before_tx_enable = 0`;
  - `uvc_runtime_flush_policy = UVC_FLUSH_POLICY_RECOVERY`;
  - `uvc_runtime_flush_on_iso_enable = 1`.

Новая проверка:

- Введен `uvc_runtime_no_frame_gap_enable = 1`.
- В `UVC_RuntimeFinishFrame()` при этом не выставляется `next_frame_tick`, даже если `uvc_frame_interval_ms != 0`.
- Цель: проверить, является ли причиной именно межкадровое окно с длинной серией 2-байтных header-only packets.
- Это диагностический режим, а не финальное согласование fps/descriptors: если он стабилизирует поток, дальше нужно будет аккуратно согласовать реальный темп кадров с UVC descriptors/probe.

## 2026-04-29 21:02: `no_frame_gap = 1` подтвердил проблему темпа

Свежий pcap после режима `uvc_runtime_no_frame_gap_enable = 1` показал новую важную вещь:

- Устройство: USB address `64`, EP `0x81`.
- В pcap есть только один ненулевой UVC URB: frame `1703`, time `1.434938 s`, `Packet Data Length = 47818`.
- Внутри URB повторяется паттерн ISO lengths `512, 512, 394`. Для текущего JPEG `1412` байт это один полный UVC frame: `510 + 510 + 392` байта JPEG плюс три 2-байтных UVC header.
- Значит `no_frame_gap = 1` отправляет десятки полных кадров в одно 16-ms host URB window, то есть намного быстрее заявленного `dwFrameInterval`.
- Счетчики это подтверждают: `cnt_eof = 33`, `cnt_data_in = 101`, `cnt_payload = 114664`, `cnt_header_only = 0`.

Вывод:

- Бесконечная отправка следующего кадра сразу после EOF не является корректным решением. Она убирает header-only окно, но нарушает темп descriptors/probe и может сама ломать поток.
- Рабочее направление: не выключать pacing полностью, а уменьшить межкадровое окно согласованно через `dwFrameInterval` / `uvc_frame_interval_ms`, чтобы не было длинной серии header-only packets.

Изменение для следующего теста:

- `uvc_runtime_no_frame_gap_enable = 0` по умолчанию.
- `UVC_FRAME_INTERVAL_100NS = 100000` (`10 ms`, диагностически около `100 fps`).
- `UVC_FRAME_RATE` теперь вычисляется из `UVC_FRAME_INTERVAL_100NS`, чтобы bitrate в descriptor не расходился с interval.
- `uvc_runtime_flush_before_tx_enable = 0`, `uvc_runtime_flush_policy = 2`, `uvc_runtime_flush_on_iso_enable = 1` оставлены как последняя рабочая база.

Что проверить:

- Если поток станет стабильнее, причина действительно в слишком длинном header-only окне при маленьком JPEG и 30 fps.
- Если поток снова сорвется, смотреть в pcap: первый сбой снова будет в header-only окне или уже на payload.
- В counters особенно важны: `cnt_eof`, `cnt_header_only`, `cnt_payload`, `cnt_iso_in_incomplete`, `cnt_flush_recovery`, `last_len`, `last_header`, `last_offset`.

## 2026-04-29 21:12: 10 ms pacing подтвердил header-only как точку срыва

Новый pcap после перехода с 30 fps на диагностические 10 ms:

- Устройство: `0483:5750`, USB address `10`, EP `0x81`.
- Полезные UVC URB больше не идут одним огромным залпом. Они идут примерно каждые `16 ms`.
- Последний ненулевой URB: frame `4775`, time `3.677778 s`, `Packet Data Length = 998`, `Isochronous transfer error count = 80`.
- Содержимое последнего URB по ISO descriptors: сначала `512`, затем `394` байта - это хвост MJPEG кадра; дальше длинная серия `2`-байтных header-only packets; затем `XACT_ERROR`.

Счетчики:

- `cnt_eof = 241`: до срыва кадры действительно завершались.
- `cnt_header_only = 18554`: даже при 10 ms остается большой поток коротких idle packets.
- `cnt_payload = 45958`.
- `cnt_flush_before_tx = 0`: per-TX flush не участвует.
- `cnt_flush_recovery = 45267`, `cnt_flush_while_epena = 45264`: после срыва recovery почти всегда flush-ит активный EP.

Вывод:

- Темп 10 ms помог отделить проблему от "мы шлем бесконечно быстро", но срыв все еще начинается в header-only участке после EOF.
- Следующая гипотеза стала сильнее: короткие 2-байтные packets в межкадровом idle window провоцируют или проявляют клин EP/core. Их надо убрать из штатного пути, а не лечить последствия recovery.

Изменение для следующего теста:

- Добавлен `uvc_runtime_idle_header_only_enable = 0` по умолчанию.
- Если `next_frame_tick` еще в будущем, `UVC_PrimeNextPacket()` не вызывает `USBD_LL_Transmit()` и увеличивает `uvc_runtime_idle_gap_skips`.
- `IsoINIncomplete` во время такого idle-gap не делает `FlushEP()` и не вызывает requeue, а увеличивает `uvc_runtime_idle_iso_skips`.
- Добавлена компактная debugger-visible структура `uvc_watch`, чтобы смотреть основные поля вместо длинного `uvc_runtime_dbg`.

Что проверять:

- `uvc_watch.cnt_header_only` должен расти намного медленнее или не расти в межкадровом ожидании.
- `uvc_watch.cnt_idle_gap_skip` должен расти между кадрами.
- Если pcap теперь покажет XACT в idle-gap, но следующий кадр после него продолжит идти, значит мы ушли от permanent stall.
- Если поток все равно зависнет на payload, тогда следующий фокус - уже не header-only, а recovery/flush при active frame.

## 2026-04-29 21:19: idle skip тоже не является решением

Результат проверки режима, где в межкадровом ожидании EP1 вообще не arm-ится:

- Устройство: `0483:5750`, USB address `12`, EP `0x81`.
- `cnt_header_only = 0`, то есть 2-байтные UVC idle packets действительно убраны.
- `cnt_idle_gap_skip = 27796`, то есть idle-gap активно пропускал `USBD_LL_Transmit`.
- Последний ненулевой URB: frame `4817`, time `4.985506 s`, `Packet Data Length = 906`, `Isochronous transfer error count = 90`.
- Содержимое последнего URB: `512 + 394` байта payload, затем много нулевых ISO descriptors, затем `XACT_ERROR`.

Вывод:

- Header-only был плохим наполнителем idle-gap, но полностью пустой idle-gap тоже плох для HS isoch IN.
- Хост продолжает выделять микрофреймы. Если устройство не arm-ит endpoint, в capture появляются пустые/ошибочные ISO descriptors; после такого участка следующий payload не восстанавливает поток.
- Поэтому нужна третья проверка: держать endpoint arm-нутым, но не отправлять UVC payload header. Самый чистый вариант - zero-length isoch packet.

Изменение для следующего теста:

- Добавлен `uvc_runtime_idle_packet_mode`.
- Режимы:
  - `0`: skip, не arm-ить EP1 в idle-gap;
  - `1`: старый header-only UVC packet;
  - `2`: zero-length isoch packet.
- По умолчанию выбран `UVC_IDLE_PACKET_ZLP`.
- Добавлен счетчик `uvc_runtime_idle_zlp_packets`, отображается как `uvc_watch.cnt_idle_zlp`.

Что проверять:

- `uvc_watch.cnt_idle_zlp` должен расти между кадрами.
- `uvc_watch.cnt_header_only` должен оставаться около нуля.
- Если pcap покажет стабильные нулевые idle packets без последующего permanent stall, это будет сильный кандидат на рабочее решение.
- Если ZLP также роняет поток, следующий шаг - менять саму структуру потока: увеличивать размер JPEG/уменьшать заявленную частоту так, чтобы payload занимал большую часть host URB window, либо пересматривать HS isoch schedule/packet size.

## 2026-04-29 21:25: idle ZLP тоже не устранил срыв потока

Свежий pcap после режима `uvc_runtime_idle_packet_mode = UVC_IDLE_PACKET_ZLP`:

- Устройство: `0483:5750`, USB address `11`, EP `0x81`.
- Header-only пакетов больше нет: `cnt_header_only = 0`.
- ZLP действительно отправляются: `cnt_idle_zlp = 35398`.
- Поток успевает передать много целых кадров: `cnt_eof = 460`.
- Ошибки остаются: `cnt_iso_in_incomplete = 16893`, `cnt_underrun = 17289`, `cnt_dropped_frames = 16874`.
- В pcap есть частичный URB frame `2873`, time `2.542069 s`, `Packet Data Length = 916`, `Isochronous transfer error count = 63`. Внутри чередуются нулевые ISO descriptors, `XACT_ERROR`, затем `512 + 404` payload, затем снова ошибки.
- После этого поток не падает мгновенно и продолжает идти до примерно `5.7 s`, но затем снова уходит в нулевые URB без устойчивого восстановления payload.

Вывод:

- Три варианта межкадрового окна уже проверены:
  - UVC header-only packets;
  - полный skip/no-arm;
  - zero-length isoch packets.
- Все три меняют форму ошибки, но не дают устойчивого решения.
- Это снижает вероятность, что первопричина в JPEG, FID/EOF, размере тестового кадра или конкретном наполнителе idle-gap.
- Более вероятно, что мы упираемся в поведение classic STM USB Device Library/HAL PCD на HS isoch IN recovery: endpoint/core после серии `XACT_ERROR` продолжает принимать `USBD_LL_Transmit()` как `USBD_OK`, но host перестает стабильно получать полезный payload.

Решение по направлению:

- Сохранить текущее состояние отдельным git checkpoint.
- Не продолжать латать idle-gap в classic stack без новой сильной гипотезы.
- Следующий чистый эксперимент - отдельная ветка с USBX `Ux_Device_Video` / ST USBX middleware, чтобы сравнить поведение на другом официальном ST stack.

## 2026-04-29 22:49: свежий CubeH7 слой поверх текущего USBX-эксперимента

После проверки USBX поведение потока осталось тем же: настоящее видео появляется, но через несколько секунд или после первого кадра поток останавливается. Это снижает вероятность, что причина только в нашем старом `usbd_uvc.c`.

Что сделано:

- Временным sparse-клоном взят официальный `STM32CubeH7` tag `v1.13.0` из GitHub ST.
- Обновлены существующие файлы `Drivers/STM32H7xx_HAL_Driver`, `Drivers/CMSIS` и `Middlewares/ST/STM32_USB_Device_Library/Core`.
- HAL поднят до `__STM32H7xx_HAL_VERSION = 1.11.6`.
- Добавлены новые CMSIS headers, которые требуются свежему `core_cm7.h`: `cachel1_armv7.h` и сопутствующие новые include-файлы.
- USBX middleware не менялся: локальные файлы `USBX/Middlewares/ST/usbx` уже совпадали с официальным `stm32-usbx-examples` `Release v1.0.0`, USBX `6.2.1`.
- Для совместимости classic UVC class со свежим `STM32_USB_Device_Library` исправлен доступ к `pdev->pUserData`: теперь используется `pdev->pUserData[pdev->classId]`, потому что в новом core это массив.

Сборка:

- `BaseH743/Debug` собирается успешно после обновления Cube: `0 errors`.
- Остались только старые предупреждения в `ili9488.c`, не связанные с USB.

Что проверяет следующий запуск:

- Если поведение не изменится и на свежем HAL/CMSIS/USB Device Core, то причина с высокой вероятностью не в устаревшем Cube-слое проекта.
- Тогда следующий фокус: низкоуровневая конфигурация OTG HS/ULPI/FIFO/interrupt priority/clocking и то, как USBX DCD получает/обрабатывает `XACT_ERROR` после isoch IN underrun.
- В свежем pcap особенно смотреть момент первого перехода от полезных EP81 payload к нулевым/ошибочным ISO descriptors: изменился ли тип/момент ошибки после HAL `1.11.6`.

## 2026-04-29 22:59: после CubeH7 v1.13.0 устройство определяется, но payload не стартует

Свежий pcap после обновления HAL/CMSIS/USB Device Core:

- Устройство `0483:5750`, USB address `28`.
- Enumeration проходит, конфигурационный дескриптор отдается.
- Windows делает UVC negotiation:
  - `GET_CUR/SET_CUR` probe;
  - `GET_MIN/GET_MAX`;
  - `SET_CUR` commit;
  - затем `SET_INTERFACE alt=1` на streaming interface `1`.
- После `SET_INTERFACE alt=1` хост запускает ISO IN URB на EP `0x81`.
- Устройство отдает ровно один 2-байтный payload: `02 00`.
- После этого в pcap идут пустые ISO URB: `Packet Data Length = 0`, явного `XACT_ERROR` на этом этапе нет.

Вывод:

- Это уже не прежняя проблема битых кадров. Сейчас после старта stream в USBX не ставится следующий payload.
- Вероятный механизм: `ux_device_class_video_write_task_function()` завершает первый IN transfer, но `transfer_request_actual_length` приходит как `0` или callback вызывается с нулевой длиной. Наш `USBD_VIDEO_StreamPayloadDone()` был почти дословно из STM example и ставил следующий payload только при `length != 0`.
- Для IN video stream это условие слишком хрупкое: если completion пришел с `actual_length = 0`, application перестает пополнять USBX payload queue, хотя host продолжает polling.

Изменение для следующего теста:

- `USBD_VIDEO_StreamPayloadDone()` теперь при любом completion, пока stream не остановлен, переводит состояние в `STREAMING` и вызывает `video_write_payload()`.
- Добавлены debugger-visible counters:
  - `usbx_video_payload_done_last_len_dbg`;
  - `usbx_video_payload_done_zero_dbg`;
  - `usbx_video_write_payload_calls_dbg`;
  - `usbx_video_write_payload_get_status_dbg`;
  - `usbx_video_write_payload_commit_status_dbg`;
  - `usbx_video_write_payload_last_len_dbg`.
- `video_write_payload()` теперь сохраняет статусы `ux_device_class_video_write_payload_get()` и `ux_device_class_video_write_payload_commit()`.

Что проверять:

- Если гипотеза верна, после первого `02 00` в pcap должны появиться пакеты `512`/остатки с JPEG payload.
- В отладчике `usbx_video_payload_done_zero_dbg` может расти, это нормально для этой проверки.
- Критичные поля при отсутствии стрима: `usbx_video_write_payload_get_status_dbg`, `usbx_video_write_payload_commit_status_dbg`, `usbx_video_write_payload_last_len_dbg`.

## 2026-04-29 23:05: USBX abort-path был скомпилирован как no-op

Проверка вопроса про функцию abort после обновления HAL/Cube:

- В текущей USBX/ST DCD функция `_ux_dcd_stm32_transfer_abort()` уже есть:
  `USBX/Middlewares/ST/usbx/common/usbx_stm32_device_controllers/ux_dcd_stm32_transfer_abort.c`.
- Она попадает в линковку: в `Debug/BaseH743.map` есть `_ux_dcd_stm32_transfer_abort`.
- HAL `1.11.6` содержит `HAL_PCD_EP_Abort()`.
- Но в `USBX/Target/ux_stm32_config.h` оставался `USBD_HAL_TRANSFER_ABORT_NOT_SUPPORTED`.
  Из-за него тело `_ux_dcd_stm32_transfer_abort()` вырезалось препроцессором, и USBX abort фактически только возвращал `UX_SUCCESS`, не вызывая `HAL_PCD_EP_Abort()` и `HAL_PCD_EP_Flush()`.

Изменение для следующего теста:

- Убран `USBD_HAL_TRANSFER_ABORT_NOT_SUPPORTED`.
- Теперь при `UX_DCD_TRANSFER_ABORT` USBX DCD должен реально делать `HAL_PCD_EP_Abort()` и `HAL_PCD_EP_Flush()` для endpoint.

Что проверять:

- Изменится ли поведение при остановке stream/alt setting/reset pipe.
- Если stream зависнет снова, сравнить pcap: исчезнут ли длинные серии пустых/ошибочных ISO URB после попыток host abort/reset.
- Если станет хуже на старте enumeration, вернуть define и считать этот abort-path несовместимым с текущей standalone-схемой.

Результат первого запуска с реальным abort-path:

- Устройство `0483:5750`, USB address `39`, перечисление проходит.
- Host делает `SET_INTERFACE alt=1` на interface `1` в `t=1.818 s`.
- После этого на EP `0x81` пришел только один UVC header-only payload `02 00`.
- Дальше ISO URB идут с `Packet Data Length = 0`, при этом в первом URB `USBD_STATUS_SUCCESS`, явного `XACT_ERROR` на старте нет.
- В `t=5.024 s` host уводит streaming interface обратно в `alt=0`.

Вывод:

- Этот конкретный отказ не похож на порчу JPEG и не похож на recovery после XACT error: полезный JPEG payload вообще не начал попадать на шину.
- Правка real abort не объясняет стартовый fail: на `alt=1` abort еще не является главным событием. Она остается важной для `alt=0/reset pipe/recovery`, но не должна быть единственным изменением в следующем чистом тесте.
- Чтобы не смешивать низкоуровневую находку с очередными попытками лечить протокол, USBX video app возвращен к STM-like поведению: `StreamPayloadDone()` снова ставит следующий payload только при `length != 0`, дополнительные `payload_done_zero/write_payload_*` counters убраны.

Следующий тест:

- Проверить сборку с единственным существенным USBX-изменением: реальный `_ux_dcd_stm32_transfer_abort()` включен, протокольный callback снова STM-like.
- Если снова будет ровно один `02 00`, следующий фокус - не UVC header/FID/JPEG, а почему USBX standalone write task не получает/не обрабатывает completion первого IN transfer или не продвигает payload ring.

## 2026-04-29 23:21: откат к последнему состоянию, где USBX-видео появлялось

После запуска без stream стало ясно, что в дереве остался слишком большой экспериментальный слой после момента, где настоящее видео уже появлялось:

- обновленный `Drivers/CMSIS`;
- обновленный `Drivers/STM32H7xx_HAL_Driver`;
- обновленный classic `Middlewares/ST/STM32_USB_Device_Library/Core`;
- совместимость `UVC/usbd_uvc.c` с новым `pUserData[pdev->classId]`;
- включение реального USBX DCD abort через удаление `USBD_HAL_TRANSFER_ABORT_NOT_SUPPORTED`.

Откат:

- `Drivers/CMSIS`, `Drivers/STM32H7xx_HAL_Driver`, `Middlewares/ST/STM32_USB_Device_Library/Core`, `UVC/usbd_uvc.c`, `USBX/Target/ux_stm32_config.h` возвращены к `f98cf06`.
- Удалены новые untracked CMSIS headers, пришедшие вместе со свежим Cube.
- `USBD_HAL_TRANSFER_ABORT_NOT_SUPPORTED` снова включен, то есть этот тест возвращает и старый no-op abort-path.
- Не тронуты `BaseH743 Debug.launch` и этот файл гипотез.

Сборка:

- `BaseH743/Debug` после отката собирается: `0 errors`, прежние `11 warnings` в `ili9488.c`.

Что проверяет следующий запуск:

- Если видео снова появится, значит срыв старта был внесен свежим Cube/HAL/Core или real-abort экспериментом, а не оставшейся логикой UVC payload.
- Если видео все равно не появится, значит отличие находится не в этих слоях, и надо смотреть pcap + debugger counters текущей сборки относительно `f98cf06`.

Результат:

- Видео вернулось.
- Значит, отсутствие stream было внесено одним из слоев, откатанных в этом шаге:
  - свежий `Drivers/CMSIS`;
  - свежий `Drivers/STM32H7xx_HAL_Driver`;
  - свежий classic `STM32_USB_Device_Library/Core`;
  - совместимость `UVC/usbd_uvc.c` под новый `pUserData[pdev->classId]`;
  - включение real USBX abort-path.

Уточнение по real abort:

- В рабочем старом HAL нет `HAL_PCD_EP_Abort()`, есть только `HAL_PCD_EP_Flush()`.
- Поэтому включить real USBX abort одним удалением `USBD_HAL_TRANSFER_ABORT_NOT_SUPPORTED` на текущей рабочей базе нельзя: сборка потребует добавить `HAL_PCD_EP_Abort()` из свежего HAL или сделать совместимость.
- Следовательно, пока не считаем real abort доказанной причиной или решением. Он остается отдельной гипотезой, которую надо проверять только изолированно.

Следующий безопасный порядок:

1. Сохранить рабочее состояние как checkpoint.
2. Проверять свежий Cube-слой не пачкой, а частями:
   - сначала только `USBX/Target/ux_stm32_config.h` без real abort не трогать;
   - затем отдельно classic USB Device Core, если он вообще нужен для USBX-сборки;
   - затем отдельно HAL PCD/LL USB;
   - затем CMSIS только если HAL требует новые headers.
3. Для real abort сделать отдельный маленький эксперимент: либо принести только `HAL_PCD_EP_Abort()`/prototype, либо заменить только минимальные PCD файлы, и сразу смотреть, стартует ли видео.

## 2026-04-29 23:25: isolated USBX abort flush-only на рабочем HAL

Гипотеза:

- Возможно, для восстановления после `alt=0`/host reset pipe/abort достаточно не полного свежего `HAL_PCD_EP_Abort()`, а очистки endpoint FIFO.
- Это близко к наблюдению из статьи iliasam: перед записью/после сбоя помогает очистка FIFO нужной конечной точки.

Изменение:

- На рабочей базе `f98cf06` не обновляем HAL/CMSIS/Core.
- В `USBX/Target/ux_stm32_config.h` вместо `USBD_HAL_TRANSFER_ABORT_NOT_SUPPORTED` включен `USBD_HAL_TRANSFER_ABORT_FLUSH_ONLY`.
- В `_ux_dcd_stm32_transfer_abort()` добавлен режим, где DCD abort вызывает только `HAL_PCD_EP_Flush()` и не требует отсутствующей в старом HAL `HAL_PCD_EP_Abort()`.

Что проверять:

- Главное: стартует ли видео так же, как до изменения.
- Если стартует, проверить поведение при остановке/закрытии приложения/повторном открытии камеры и при зависании stream.
- Если видео опять не стартует или исчезает, flush-only abort сам ломает рабочее состояние и его надо убрать.

Результат:

- Видео стартует, значит flush-only abort не ломает рабочую USBX-сборку.
- Потом stream останавливается так же, как раньше. Значит одна только очистка FIFO в `_ux_dcd_stm32_transfer_abort()` не является лечением текущего зависания.
- Следующий фокус - не менять протокол, а снять внутреннее состояние USBX video write task в момент остановки.

## 2026-04-29 23:40: диагностика внутреннего состояния USBX video/DCD

Цель:

- Понять, где именно замирает поток после визуальной остановки: producer/ring buffer, USBX video write task, endpoint transfer request или DCD abort/flush.
- Не менять payload, FID, descriptors, FPS и JPEG path в этом шаге.

Добавлены debugger-visible переменные:

- `usbx_video_stream_ptr_dbg`
- `usbx_video_endpoint_ptr_dbg`
- `usbx_video_task_state_dbg`
- `usbx_video_task_status_dbg`
- `usbx_video_buffer_error_count_dbg`
- `usbx_video_transfer_len_dbg`
- `usbx_video_access_len_dbg`
- `usbx_video_transfer_status_dbg`
- `usbx_video_transfer_completion_dbg`
- `usbx_video_transfer_actual_len_dbg`
- `usbx_video_transfer_requested_len_dbg`
- `usbx_dcd_abort_calls_dbg`
- `usbx_dcd_abort_ep_dbg`
- `usbx_dcd_abort_flush_status_dbg`

Как читать:

- Если `usbx_task_calls_dbg` растет, а `usbx_video_task_state_dbg` стоит в reset/exit с ошибкой в `usbx_video_task_status_dbg`, проблема внутри USBX write task/transfer completion.
- Если `usbx_video_buffer_error_count_dbg` растет, USBX видит рассинхрон своего payload ring: либо producer не успевает, либо access/transfer позиции сталкиваются.
- Если `usbx_video_transfer_status_dbg` остается pending, а `usbx_video_transfer_completion_dbg` не меняется, зависание ближе к endpoint/DCD interrupt completion.
- Если `usbx_video_transfer_len_dbg == 0`, а `usbx_video_access_len_dbg` содержит payload, надо смотреть продвижение transfer_pos/access_pos.
- Если `usbx_dcd_abort_calls_dbg` растет в момент закрытия/перезапуска камеры, смотреть `usbx_dcd_abort_ep_dbg` и `usbx_dcd_abort_flush_status_dbg`.

Сборка:

- `BaseH743/Debug`: `0 errors`.

Результат первого скрина после зависания:

- `usbx_video_task_state_dbg = 34` (`RW_WAIT`).
- `usbx_video_transfer_status_dbg = 1` (`PENDING`).
- `usbx_video_transfer_len_dbg = 512`, `requested_len = 512`, `actual_len = 0`.
- `usbx_video_task_status_dbg = 0`: USBX class не увидел ошибку верхнего уровня.
- `usbx_dcd_abort_calls_dbg = 0`: host/class abort path не участвовал.
- Вывод: UVC class не остановлен и не занят producer/JPEG. Зависание ниже - pending IN transfer на EP1 не получает completion/recovery.

Найдено важное совпадение с прежней classic UVC победой:

- В старом HAL обработчик `GINTSTS_IISOIXFR` вызывает `HAL_PCD_ISOINIncompleteCallback(hpcd, 0)`, то есть передает `epnum = 0` для global incomplete ISO IN event.
- USBX DCD callback использовал этот `epnum` как индекс endpoint и поэтому пытался recovery не для EP `0x81`, а фактически для EP0.
- Это объясняет зависший `PENDING` transfer: EP1 после incomplete event не переармировался корректно.

Изменение:

- В `USBX/Middlewares/ST/usbx/common/usbx_stm32_device_controllers/ux_dcd_stm32_callback.c` добавлен remap/find активного ISO IN endpoint при `epnum == 0`.
- `HAL_PCD_ISOINIncompleteCallback()` теперь повторяет pending transfer именно на найденном ISO IN endpoint, ожидаемо `0x81`.
- Добавлены counters:
  - `usbx_dcd_data_in_calls_dbg`
  - `usbx_dcd_data_in_ep_dbg`
  - `usbx_dcd_iso_incomplete_calls_dbg`
  - `usbx_dcd_iso_incomplete_hal_ep_dbg`
  - `usbx_dcd_iso_incomplete_mapped_ep_dbg`
  - `usbx_dcd_iso_incomplete_retry_dbg`
  - `usbx_dcd_iso_incomplete_retry_status_dbg`
  - `usbx_dcd_iso_incomplete_no_ep_dbg`

Что проверить:

- При следующем зависании/долгом прогоне `usbx_dcd_iso_incomplete_hal_ep_dbg` может быть `0`, но `usbx_dcd_iso_incomplete_mapped_ep_dbg` должен стать `0x81` (`129`).
- `usbx_dcd_iso_incomplete_retry_dbg` должен расти вместе с incomplete events.
- Если stream теперь не зависает, корень был тем же классом ошибки, что и в classic UVC: неверная привязка global `IISOIXFR` к EP0.
- Если зависнет снова, следующий скрин должен показать, остался ли `transfer_status=1/PENDING` при уже правильном `mapped_ep=129`.

Результат проверки immediate retry:

- Stream не стартует.
- В debugger:
  - `usbx_video_task_state_dbg = 34` (`RW_WAIT`);
  - `usbx_video_transfer_status_dbg = 1` (`PENDING`);
  - `usbx_video_transfer_len_dbg = 512`, `actual_len = 0`;
  - `usbx_dcd_iso_incomplete_hal_ep_dbg = 0`;
  - `usbx_dcd_iso_incomplete_mapped_ep_dbg = 129` (`0x81`);
  - `usbx_dcd_iso_incomplete_retry_dbg` растет, `retry_status = 0`.
- В pcap для нашего устройства `0483:5750`, address `46`: полезного payload нет; EP `0x81` почти сразу уходит в `USBD_STATUS_ISOCH_REQUEST_FAILED (0xc0000b00)` и затем пачки `USBD_STATUS_CANCELED (0xc0010000)`.

Вывод:

- Remap EP0 -> EP1 правильный и реально срабатывает.
- Но immediate `HAL_PCD_EP_Transmit()` из `HAL_PCD_ISOINIncompleteCallback()` поверх текущего pending transfer создает retry storm и не восстанавливает поток.

Следующее изменение:

- Убрать immediate retry из `IISOIXFR` callback.
- Вместо этого помечать текущий USBX transfer как завершенный с `UX_TRANSFER_MISSED_FRAME`, `actual_length = requested_length` и ставить `UX_DCD_STM32_ED_STATUS_DONE`.
- Идея: не долбить hardware endpoint из ISR, а вернуть standalone video write task из `RW_WAIT`, дать ему освободить потерянный payload и штатно поставить следующий.
- Добавлен счетчик `usbx_dcd_iso_incomplete_done_dbg`.

## 2026-04-30: тест без правок протокола, только обновление HAL/CMSIS

Причина:

- Одни и те же попытки recovery на classic USB Device Library и USBX дают похожую картину: иногда устройство не определяется, иногда поток стартует и зависает.
- Это ослабляет гипотезу, что корень именно в UVC class/payload state machine.
- Следующий чистый тест: вернуть USBX/UVC к последнему чекпоинту и менять только базовые библиотеки STM.

Сделано:

- USBX/UVC protocol recovery patches возвращены к чекпоинту, без новых изменений в механике передачи.
- HAL/CMSIS обновлены из `STM32CubeH7 v1.13.0`.
- HAL Driver: `STM32H7xx HAL v1.11.6`.
- CMSIS Device H7: `V1.10.7`.
- CMSIS Include обновлен; добавлены новые зависимые headers `cachel1_armv7.h`, `core_cm55.h`, `core_cm85.h`, `core_starmc1.h`, `pac_armv81.h`, `pmu_armv8.h`.

Сборка:

- `Debug/BaseH743.elf` собирается успешно.
- Оставшиеся warnings относятся к `Core/Src/ili9488.c` и не связаны с USB/HAL update.

Что проверяем на плате:

- Определяется ли устройство хостом после полного power cycle.
- Меняется ли частота `IsoINIncomplete` / зависаний относительно предыдущей USBX-сборки.
- Если поведение не изменится, смотреть ниже UVC class: `stm32h7xx_hal_pcd.c`, `stm32h7xx_ll_usb.c`, настройки ULPI/PHY/FIFO/interrupt handling и аппаратные условия линии.

## 2026-04-30: свежий pcap после HAL/CMSIS и обновление USBX middleware

Наблюдение из pcap:

- Устройство `0483:5750` успешно перечисляется, в последней записи получило address `54`.
- Хост реально включает streaming interface: `SET_INTERFACE` виден примерно на `1.283 s`.
- Поток не полностью отсутствует: первый ISO/UVC payload на EP `0x81` появляется примерно на `1.305 s`, первые байты `02 00`.
- До срыва есть `26` ненулевых URB на EP `0x81`.
- Перед остановкой появляются ISO errors внутри успешных URB: например `iso_error_count = 1`, затем `4`, последний полезный URB имеет `data_len = 480`, `iso_error_count = 16` и начинается уже с середины JPEG, не с чистого UVC header.
- После этого EP `0x81` уходит в повторяющиеся `USBD_STATUS_ISOCH_REQUEST_FAILED (0xc0000b00)` и пачки `USBD_STATUS_CANCELED (0xc0010000)`.

Вывод:

- Это не похоже на ошибку descriptors/SET_INTERFACE и не похоже на полное отсутствие JPEG.
- Хост поток включает и первые данные принимает.
- Срыв происходит ниже UVC application layer: после ISO error DCD/HAL/USBX не возвращают устойчивое состояние endpoint/transfer.

Сделано для чистого библиотечного теста:

- USBX middleware обновлен из официального `STMicroelectronics/stm32-mw-usbx v6.4.300`.
- Текущая версия USBX core: `6.4.3` (`USBX_MAJOR_VERSION=6`, `USBX_MINOR_VERSION=4`, `USBX_PATCH_VERSION=3`).
- Обновлены слои:
  - `USBX/Middlewares/ST/usbx/common/core`
  - `USBX/Middlewares/ST/usbx/common/usbx_device_classes`
  - `USBX/Middlewares/ST/usbx/common/usbx_stm32_device_controllers`
  - `USBX/Middlewares/ST/usbx/ports/cortex_m7/gnu`
- UVC descriptors/payload/protocol специально не менялись.
- Для standalone-сборки добавлены совместимые utility-заглушки/прототипы, требуемые новым USBX: `_ux_utility_time_elapsed()` и прототипы interrupt/time helpers в `ux_user.h`.

Сборка:

- `Debug/BaseH743.elf` собирается успешно после обновления USBX.
- Размер текущего образа: `text=199624`, `data=456`, `bss=491472`.

Что проверить на плате:

- Стартует ли stream с новым USBX/DCD при тех же descriptors и той же application-логике.
- Если зависание повторится, сравнить pcap: остались ли те же `USBD_STATUS_ISOCH_REQUEST_FAILED`/`CANCELED` после первых payload.
- Если картина не изменилась даже с новым HAL/CMSIS и новым USBX, следующая зона поиска уже не UVC state machine, а физика/настройка USB HS: ULPI PHY, clocking, FIFO sizing, interrupt latency/priority, PCD low-level endpoint state.

## 2026-04-30: после USBX update stream не стартует, только один header-only packet

Наблюдение из свежего pcap:

- Устройство `0483:5750` перечисляется, address `13`.
- UVC Probe/Commit проходят успешно.
- Хост делает `SET_INTERFACE alt=1` на frame `2111`, время примерно `2.096 s`.
- На EP `0x81` есть `516` URB, но ненулевой payload только один:
  - frame `2147`, время `2.117 s`, `data_len = 2`, статус `USBD_STATUS_SUCCESS`.
- После этого идут успешные нулевые ISO URB, без `USBD_STATUS_ISOCH_REQUEST_FAILED`, без `CANCELED`, без `iso_error_count`.

Вывод:

- Это новый режим отказа после USBX update: не прежний срыв после ISO errors.
- `Abort`/`IsoINIncomplete` в этой записи не выглядит причиной, потому что ошибок ISO в pcap нет.
- Похоже, приложение ставит первый 2-байтовый UVC header-only payload в состоянии `READY`, но после него не переходит к нормальным JPEG payload.
- Возможная причина: новый/обновленный standalone USBX video path возвращает payload-done с нулевой длиной или иначе не запускает нашу ветку `if (length != 0U)`, поэтому `uvc_state` остается `READY`.
- Дополнительная найденная слабость: `packet_index` был `static` внутри `video_write_payload()` и не сбрасывался при новом `alt=1`, поэтому новый stream мог начаться с середины JPEG.

Изменение:

- В `USBD_VIDEO_StreamChange(alt=1)` поток теперь стартует сразу как `UVC_PLAY_STATUS_STREAMING`, без header-only priming packet.
- Перед `ux_device_class_video_transmission_start()` предварительно коммитятся два реальных payload, чтобы не зависеть от первого payload-done callback.
- В `USBD_VIDEO_StreamPayloadDone()` следующий payload готовится даже если `length == 0`, пока stream не остановлен.
- `video_packet_index` вынесен в состояние модуля и сбрасывается на `alt=0` и `alt=1`.
- Добавлены компактные counters:
  - `usbx_video_last_alt_dbg`
  - `usbx_video_start_status_dbg`
  - `usbx_video_write_calls_dbg`
  - `usbx_video_get_status_dbg`
  - `usbx_video_commit_status_dbg`
  - `usbx_video_last_done_len_dbg`
  - `usbx_video_last_payload_len_dbg`
  - `usbx_video_last_buffer_len_dbg`
  - `usbx_video_last_state_dbg`

Сборка:

- После USBX update выполнена чистая пересборка, чтобы исключить старые объектники middleware.
- `Debug/BaseH743.elf` собирается успешно.
- Размер: `text=199784`, `data=456`, `bss=491504`.

Что проверить:

- В pcap после `SET_INTERFACE alt=1` первый ненулевой payload должен быть уже около `512` байт, а не `2`.
- В debugger:
  - `usbx_video_last_alt_dbg = 1`;
  - `usbx_video_start_status_dbg = 0`;
  - `usbx_video_get_status_dbg = 0`;
  - `usbx_video_commit_status_dbg = 0`;
  - `usbx_video_last_payload_len_dbg` должен быть `512` или последний короткий payload кадра.
- Если снова будет только один пакет, смотреть `usbx_video_payload_done_dbg` и `usbx_video_last_done_len_dbg`: это покажет, вызывает ли USBX callback после первого IN transfer.

## 2026-04-30: payload 512 готов, но на шину не выходит

Наблюдение:

- Debugger после правки показывает:
  - `usbx_video_start_status_dbg = 0`;
  - `usbx_video_get_status_dbg = 0`;
  - `usbx_video_commit_status_dbg = 0`;
  - `usbx_video_last_payload_len_dbg = 512`.
- Значит USBX application/video layer реально подготовил и закоммитил 512-байтовый payload.

Свежий pcap:

- Устройство `0483:5750` перечисляется, address `24`.
- На `EP 0x81` найдено `2980` URB.
- Ненулевых payload на `EP 0x81`: `0`.
- Первые URB на `EP 0x81` после `alt=1` идут с `data_len = 0`.
- Первый ISO warning/error внутри URB: frame `3359`, время `2.229 s`, `iso_error_count = 35`.
- Первый явный bad status: frame `3385`, время `2.245 s`, `USBD_STATUS_ISOCH_REQUEST_FAILED (0xc0000b00)`, `iso_error_count = 128`.
- Потом идут пачки `USBD_STATUS_CANCELED (0xc0010000)`.

Вывод:

- Ошибка уже не в генерации JPEG и не в `video_write_payload()`: 512 байт есть в USBX payload queue.
- Ошибка находится ниже: USBX standalone write task / DCD transfer_run / HAL_PCD_EP_Transmit / PCD endpoint state.
- Сейчас нужно понять, вызывается ли фактически `HAL_PCD_EP_Transmit(EP 0x81, ..., 512)`.

Добавлена диагностика DCD:

- В `_ux_dcd_stm32_transfer_run()`:
  - `usbx_dcd_run_calls_dbg`
  - `usbx_dcd_run_ep_dbg`
  - `usbx_dcd_run_phase_dbg`
  - `usbx_dcd_run_len_dbg`
  - `usbx_dcd_run_ed_status_before_dbg`
  - `usbx_dcd_run_ed_status_after_dbg`
  - `usbx_dcd_run_wait_transfer_dbg`
  - `usbx_dcd_run_done_seen_dbg`
  - `usbx_dcd_run_tx_calls_dbg`
  - `usbx_dcd_run_tx_status_dbg`
- В `HAL_PCD_DataInStageCallback()`:
  - `usbx_dcd_data_in_calls_dbg`
  - `usbx_dcd_data_in_ep_dbg`
  - `usbx_dcd_data_in_req_len_dbg`
- В `HAL_PCD_ISOINIncompleteCallback()`:
  - `usbx_dcd_iso_incomplete_calls_dbg`
  - `usbx_dcd_iso_incomplete_ep_dbg`
  - `usbx_dcd_iso_incomplete_retry_calls_dbg`
  - `usbx_dcd_iso_incomplete_retry_status_dbg`
  - `usbx_dcd_iso_incomplete_retry_len_dbg`

Что проверить после прошивки:

- Если `usbx_dcd_run_tx_calls_dbg == 0`: USBX video task не доходит до DCD transfer start, копать task scheduling/state.
- Если `usbx_dcd_run_tx_calls_dbg > 0`, `usbx_dcd_run_tx_status_dbg == 0`, но `usbx_dcd_data_in_calls_dbg == 0`: HAL принял transmit, но IN completion не приходит; это PCD/HAL/PHY/ISO timing/state.
- Если `usbx_dcd_run_tx_status_dbg != 0`: HAL сразу отвергает transmit; смотреть endpoint state/FIFO/PCD busy.
- Если растет `usbx_dcd_iso_incomplete_calls_dbg` до первого `DataIn`: первый IN transfer промахивается по ISO frame, а immediate retry из callback снова может создавать retry storm.

Сборка:

- `Debug/BaseH743.elf` собирается успешно.
- Размер: `text=200020`, `data=456`, `bss=491600`.

## 2026-04-30: после обновления pcap показывает ISO IN без payload

Наблюдение по свежему `C:\Users\Professional\Documents\UVC.pcapng`:

- Устройство `0483:5750` перечисляется как USB address `27`.
- Probe/Commit проходят, затем хост делает `SET_INTERFACE alt=1`:
  - frame `2650`, время `1.544625 s`, `bAlternateSetting = 1`, `wInterface = 1`;
  - ответ устройства frame `2652`.
- Сразу после этого хост ставит серию `URB_ISOCHRONOUS in` на `EP 0x81`.
- Первый завершенный ISO URB от устройства:
  - frame `2679`, время `1.566397 s`;
  - `Endpoint: 0x81 IN`;
  - `Packet Data Length = 0`;
  - `number of packets = 128`;
  - `error count = 0`;
  - все ISO packet entries имеют `ISO Data length = 0` и `USBD_STATUS_SUCCESS`.
- При закрытии камеры хост делает `ABORT_PIPE`, `SYNC_RESET_PIPE_AND_CLEAR_STALL`, затем `SET_INTERFACE alt=0`:
  - frames `8907..8911`, время около `5.086 s`.

Вывод:

- В этом pcap нет реального payload на `EP 0x81` вообще, но и нет ISO ошибок.
- Это отличается от прежней картины с `USBD_STATUS_ISOCH_REQUEST_FAILED/CANCELED`.
- С учетом debugger-значений `usbx_video_last_payload_len_dbg = 512`, `usbx_dcd_run_tx_calls_dbg > 0`, `usbx_dcd_data_in_calls_dbg > 0`, проблема сейчас ниже application/video queue:
  - либо DCD/HAL реально программирует `EP1` на нулевую длину;
  - либо данные есть в `HAL_PCD_EP_Transmit()`, но FIFO/endpoint не отдает их в ISO slots;
  - либо callbacks, которые мы считаем `DataIn`, приходят не от `EP1`.

Найденный забытый пункт после HAL update:

- В `USBX/Target/ux_stm32_config.h` оставался `USBD_HAL_TRANSFER_ABORT_NOT_SUPPORTED`.
- В обновленном HAL функция `HAL_PCD_EP_Abort()` уже есть, а в официальном USBX reference этого define нет.
- Define удален, теперь `_ux_dcd_stm32_transfer_abort()` может выполнять официальный `HAL_PCD_EP_Abort()` + `HAL_PCD_EP_Flush()`.

Добавлена диагностика вокруг `HAL_PCD_EP_Transmit()` в `_ux_dcd_stm32_transfer_run()`:

- `usbx_dcd_run_data_ptr_dbg`
- `usbx_dcd_run_first_word_dbg`
- `usbx_dcd_run_second_word_dbg`
- `usbx_dcd_pcd_epnum_dbg`
- `usbx_dcd_pcd_xfer_len_dbg`
- `usbx_dcd_pcd_xfer_count_dbg`
- `usbx_dcd_pcd_maxpacket_dbg`
- `usbx_dcd_pcd_type_dbg`
- `usbx_dcd_pcd_dieptsiz_dbg`
- `usbx_dcd_pcd_diepctl_dbg`
- `usbx_dcd_pcd_dtxfsts_dbg`
- `usbx_dcd_pcd_diepint_dbg`

Как интерпретировать следующий запуск:

- Если `usbx_dcd_run_ep_dbg = 0x81`, `usbx_dcd_run_len_dbg = 512`, `usbx_dcd_run_first_word_dbg` начинается с UVC header/JPEG данных, а `usbx_dcd_pcd_dieptsiz_dbg` содержит `XFRSIZ=512` и `PKTCNT=1`, значит USBX и HAL передают корректный payload, а проблема уже в ISO timing/FIFO/PCD low-level.
- Если `usbx_dcd_run_len_dbg = 0` или `usbx_dcd_run_first_word_dbg = 0`, значит payload теряется между USBX video queue и DCD transfer.
- Если `usbx_dcd_data_in_ep_dbg != 1`, значит текущие `DataIn` callbacks не подтверждают передачу video endpoint.
- Если `usbx_dcd_pcd_type_dbg != 1`, endpoint открыт не как ISO.

Сборка:

- `make all -j8` через toolchain STM32CubeIDE 2.0.0 проходит успешно.
- Размер: `text=200368`, `data=456`, `bss=491632`.
## 2026-04-30: USBX-ветка не восстанавливала маску IISOIXFR

Наблюдение по свежему снимку отладчика:

- USBX реально запускает валидную ISO IN передачу на EP `0x81`:
  - `usbx_dcd_run_ep_dbg = 129`;
  - `usbx_dcd_run_len_dbg = 512`;
  - первый word в FIFO `0xD8FF0102`, байты `02 01 FF D8`: это корректный 2-байтовый UVC header и начало JPEG.
- HAL/LL программирует endpoint корректно:
  - `DIEPTSIZ = 0x20080200`: `MULCNT=1`, `PKTCNT=1`, `XFRSIZ=512`;
  - свободное место Tx FIFO падает с `768` до `640` words, то есть в FIFO записан ровно один 512-байтовый пакет.
- Pcap при этом показывает ISO completions на EP `0x81` с нулевой длиной payload.
- `usbx_dcd_iso_incomplete_calls_dbg` растет очень быстро, а нормальный EP1 `DataIn` не приходит.

Вывод:

- Это не указатель JPEG, не генерация UVC header и не порча очереди payload в USBX.
- Пакет уже лежит в USB FIFO, после чего путь OTG/HAL по `IISOIXFR` абортит EP1 раньше, чем payload доходит до хоста.
- В старой classic-library ветке у нас был helper для маскирования `IISOIXFR`, но новый USBX init path его не вызывал.

Изменение:

- `usb_mask_iisoixfr_enable` по умолчанию выставлен в `1`.
- USBX теперь маскирует `USB_OTG_GINTMSK_IISOIXFRM` после `HAL_PCD_Init()` и после `HAL_PCD_Start()`.
- Добавлена диагностика:
  - `usbx_gintmsk_after_start_dbg`;
  - `usbx_gintsts_after_start_dbg`;
  - `usbx_gintmsk_after_iisoixfr_mask_dbg`.

Что проверить после прошивки:

- `usb_mask_iisoixfr_enable == 1`.
- `usb_gintmsk_after_iisoixfr_mask` и `usbx_gintmsk_after_iisoixfr_mask_dbg` не должны содержать бит `0x00100000`.
- `usbx_dcd_iso_incomplete_calls_dbg` не должен расти непрерывной лавиной.
- Если начнет расти EP1 `DataIn`, а pcap покажет ненулевой payload на EP `0x81`, значит реальная причина срыва была в HAL abort по `IISOIXFR`.

## 2026-04-30: контрольная точка - USBX UVC снова показывает видео

Результат после маскирования `IISOIXFR` в USBX-ветке:

- Картинка появилась на хосте.
- Видео длилось несколько секунд, тестовый ролик успел прокрутиться примерно 2.5 раза.
- Визуально это первый успешный USBX/HAL-update результат после серии нулевых ISO completions.

Свежий pcap `C:\Users\Professional\Documents\UVC.pcapng`:

- Файл обновлен `2026-04-30 18:47:08`, размер `8513332` bytes.
- Устройство в этом захвате работает как USB address `28`.
- `SET_INTERFACE alt=1`: frame `2515`, время `1.654497 s`.
- Первый ненулевой ISO payload на EP `0x81`: frame `2549`, время `1.676466 s`, `data_len = 1536`.
- Всего ISO URB на EP `0x81`: `4348`.
- Ненулевых ISO URB на EP `0x81`: `90`.
- Суммарно полезных bytes в этих URB: `269191`.
- Максимальный payload в одном URB: `4565` bytes.
- Последний ненулевой payload: frame `11617`, время `7.772477 s`, `data_len = 3480`.
- Между первым и последним ненулевым payload прошло примерно `6.096 s`.
- Плохих `urb_status` по device address `28` не найдено.
- `usb.iso.error_count > 0` не найдено.
- После frame `11617` хост продолжает получать ISO URB на EP `0x81`, но уже с `data_len = 0` и успешным статусом.
- `SET_INTERFACE alt=0`/остановка хостом позже: frame `19397`, время `10.359504 s`.

Вывод по этой контрольной точке:

- Победная часть подтверждена: маскирование `IISOIXFR` убрало прежнюю ситуацию, когда HAL абортил EP1 до доставки payload.
- Текущий срыв уже другой природы: USB не падает с ошибкой, а поток после нескольких секунд перестает получать ненулевые payload.
- Наиболее вероятное направление дальше: producer/USBX video payload queue/standalone task state. Нужно понять, почему после последнего нормального payload `video_write_payload()` или USBX write task перестают подкармливать очередь, хотя endpoint и host URB остаются живыми.

Следующие проверки:

- Добавить компактную диагностику состояния `stream_write`:
  - `ux_device_class_video_stream_task_state`;
  - `ux_device_class_video_stream_task_status`;
  - `ux_device_class_video_stream_buffer_error_count`;
  - длина payload в `transfer_pos`;
  - длина payload в `access_pos`;
  - `usbx_video_get_status_dbg`;
  - `usbx_video_commit_status_dbg`;
  - `usbx_video_write_calls_dbg`;
  - `usbx_video_payload_done_dbg`.
- Убрать/пересмотреть `ux_utility_delay_ms(USBD_VIDEO_IMAGE_LAPS)` внутри `video_write_payload()`: сейчас pcap показывает ритм ненулевых payload в основном `80 ms`, что совпадает с этой задержкой. Лучше управлять FPS внешним таймером/producer state, а не блокировать USBX payload-done путь.
- Если после остановки `get_status` или `commit_status` становится `UX_BUFFER_OVERFLOW`/`UX_ERROR`, чинить ownership кольцевых payload buffers.
- Если task state уходит в stop/reset/error, чинить USBX video task state.

## 2026-04-30: подготовка следующего USBX-прогона после сверки descriptors

Что изменено перед следующим тестом:

- UVC frame interval теперь задан через периоды:
  - FS: `UVC_FRAME_PERIOD_FS_MS = 100`, `UVC_FRAME_INTERVAL_FS = 1000000`;
  - HS: `UVC_FRAME_PERIOD_HS_MS = 80`, `UVC_FRAME_INTERVAL_HS = 800000`.
- Убран вводящий в заблуждение `UVC_CAM_FPS_HS`: реальная HS скорость этой сборки `12.5 fps`, а не целые `12 fps`.
- `dwMinBitRate/dwMaxBitRate` считаются от `UVC_MAX_FRAME_SIZE`, а не от несжатого `width * height * 16`.
- `UVC_MAX_FRAME_SIZE` для USBX выставлен в `8192`, чтобы соответствовать текущим тестовым MJPEG кадрам и classic UVC ветке, а не шаблонному буферу `604160`.
- `VS_PROBE GET_*` и `VS_COMMIT GET_CUR` теперь возвращают согласованные `dwFrameInterval`, `dwMaxPayloadTransferSize`, `dwMaxVideoFrameSize` и `dwClockFrequency`.
- Добавлена компактная диагностика `stream_write`:
  - `usbx_video_stream_task_state_dbg`;
  - `usbx_video_stream_task_status_dbg`;
  - `usbx_video_stream_error_dbg`;
  - `usbx_video_stream_buffer_error_count_dbg`;
  - `usbx_video_stream_buffer_size_dbg`;
  - `usbx_video_stream_payload_buffer_size_dbg`;
  - `usbx_video_stream_transfer_pos_dbg`;
  - `usbx_video_stream_access_pos_dbg`;
  - `usbx_video_stream_transfer_len_dbg`;
  - `usbx_video_stream_access_len_dbg`;
  - `usbx_video_stream_endpoint_addr_dbg`;
  - `usbx_video_stream_endpoint_mps_dbg`.

Сборка:

- `make -C Debug all -j8` через STM32CubeIDE 2.0.0 toolchain проходит успешно.
- Размер после добавления diagnostics: `text=201560`, `data=456`, `bss=491824`.

Что проверить после прошивки:

- До старта stream: endpoint должен стать `0x81`, `mps = 512`.
- Во время нормального потока:
  - `usbx_video_get_status_dbg = 0`;
  - `usbx_video_commit_status_dbg = 0`;
  - `usbx_video_stream_buffer_error_count_dbg` не должен расти;
  - `usbx_video_stream_transfer_len_dbg` и `usbx_video_stream_access_len_dbg` должны показывать, что кольцо payload не пустое постоянно.
- После остановки ненулевых ISO payload в pcap:
  - если `transfer_len/access_len` стали `0`, а `write_calls/payload_done` перестали расти, копать starvation очереди payload;
  - если `buffer_error_count` растет, чинить ownership/ring buffer USBX video payload;
  - если `task_state/task_status` ушли в reset/error, копать standalone USBX video task state;
  - если очередь выглядит живой, но pcap получает только `data_len = 0`, снова смотреть DCD/HAL/FIFO path.

## 2026-04-30: свежий USBX pcap после сверки descriptors

Файл `C:\Users\Professional\Documents\UVC.pcapng`:

- Обновлен `2026-04-30 19:18:31`, размер `5533884` bytes.
- Устройство в захвате работает как USB address `33`.
- `SET_INTERFACE alt=1`: frame `1933`, время `1.452466 s`.
- Descriptor/probe в pcap согласованы с HS режимом:
  - `dwFrameInterval = 800000`, то есть `80 ms`;
  - `dwMaxPayloadTransferSize = 512`;
  - `dwMaxVideoFrameSize = 8192`;
  - `dwClockFrequency = 48000000`.
- EP `0x81`: всего `3276` URB rows, из них `47` ненулевых и `3229` нулевых.
- Восстановлено `40` завершенных UVC/MJPEG кадров, незавершенного хвоста нет.
- Первый ненулевой UVC payload: frame `1983`, время `1.474526 s`.
- Последний ненулевой UVC payload: frame `6181`, время `4.786525 s`, `data_len = 1004`.

Ключевая картина по месту срыва:

- До финального срыва есть два одиночных ISO `XACT_ERROR`:
  - row `4329`, `3.218522 s`, `iso_error_count = 1`: кадр `#21`, ожидаемый `JPEG21_SIZE = 4303`, восстановлено `3793`, не хватает ровно одного `510`-байтного payload chunk; `SOI/EOI` есть.
  - row `5725`, `4.370532 s`, `iso_error_count = 1`: кадр `#35`, ожидаемый `JPEG6_SIZE = 3309`, восстановлено `2799`, не хватает ровно одного `510`-байтного payload chunk; `SOI` отсутствует, `EOI` есть.
- Финальный срыв не происходит внутри последнего JPEG:
  - кадр `#39`, ожидаемый `JPEG10_SIZE = 4060`, восстановлен полностью, `SOI/EOI` есть;
  - EOF этого кадра приходит в row `6181`, descriptor `86`, microframe `169036340`;
  - в том же URB после EOF идут `40` ISO descriptors с `USBD_STATUS_XACT_ERROR`;
  - следующий URB row `6199`, `4.802527 s`, уже полностью `128/128 XACT_ERROR`, `data_len = 0`;
  - дальше поток уходит в нулевые/ошибочные URB без восстановления payload.

Вывод:

- Это уже не тот же pcap-паттерн, что предыдущий прогон с address `28`, где после последнего payload не было `usb.iso.error_count > 0` и дальше шли успешные нулевые URB.
- Если под "тем же местом" понимать конкретный JPEG index/offset, то нет: одиночные потери пришлись на `JPEG21` и `JPEG6`, а финальный срыв случился сразу после полного `JPEG10`.
- Если под "тем же местом" понимать фазу передачи, то похоже: критический срыв начинается на границе после завершенного кадра, когда нужно перейти к следующему payload/следующему кадру.
- Одиночные `XACT_ERROR` снова совпадают с отсутствием ровно одного UVC payload chunk на шине. Это возвращает в подозреваемые DCD/HAL/FIFO/ISO scheduling path, а не только USBX payload queue starvation.

Что проверить в отладчике:

- `usbx_video_stream_task_state_dbg = 33` означает `UX_DEVICE_CLASS_VIDEO_STREAM_RW_START` (`UX_STATE_STEP + 1`, при `UX_STATE_STEP = 0x20`), а не само по себе ошибку.
- Watch expressions должны использовать полные имена:
  - `usbx_video_stream_task_status_dbg`;
  - `usbx_video_stream_buffer_error_count_dbg`;
  - `usbx_video_stream_transfer_len_dbg`;
  - `usbx_video_stream_access_len_dbg`;
  - `usbx_video_get_status_dbg`;
  - `usbx_video_commit_status_dbg`;
  - `usbx_video_write_calls_dbg`;
  - `usbx_video_payload_done_dbg`.
- Эти symbols не выкинуты линкером: они есть в `Debug/BaseH743.map` в `.bss` около `0x2400411c..0x24004180`.

Следующие действия:

- Считать текущий pcap подтверждением, что после descriptor-сверки проблема снова видна как низкоуровневый ISO IN `XACT_ERROR`.
- В следующем прогоне ловить значения debug counters сразу после первого одиночного `XACT_ERROR` и после финального row `6181`-подобного срыва.
- Если USBX counters выглядят живыми (`get/commit = 0`, очередь не пустая, task state не error), копать DCD/HAL обработку ISO IN completion/error и состояние EP1/FIFO вокруг первого `XACT_ERROR`.

## 2026-04-30 19:37:25: новый pcap с DCD debug counters

Файл `C:\Users\Professional\Documents\UVC.pcapng`:

- Обновлен `2026-04-30 19:37:25`, размер `11779756` bytes.
- Устройство в захвате: USB address `36`.
- `SET_INTERFACE alt=1`: frame `2645`, время `1.431008 s`.
- `SET_INTERFACE alt=0`: frame `19269`, время `7.215596 s`.
- EP `0x81`, ISO IN rows с `128` descriptors: `6568`.
- Ненулевые ISO IN URB: `19`.
- Первые данные: frame `2689`, `1.452519 s`, `data_len = 1024`.
- Последние данные: frame `4819`, `2.764540 s`, `data_len = 979`.
- Первый error после старта потока:
  - frame `4467`, `2.524524 s`, `data_len = 3450`, `iso_error_count = 1`;
  - это одиночная потеря одного `510`-byte UVC payload chunk.
- Финальный срыв:
  - frame `4827`, `2.780528 s`, `data_len = 0`, `iso_error_count = 127`;
  - frame `4861`, `2.796521 s`, `data_len = 0`, `USBD_STATUS_ISOCH_REQUEST_FAILED`, `iso_error_count = 128`;
  - дальше payload data уже не восстанавливается до `SET_INTERFACE alt=0`.

Реконструкция MJPEG/UVC payload:

- Восстановлено `16` завершенных кадров.
- Последовательность размеров совпадает с `JPEG0..JPEG15` из `USBX/Stream/stream1.h`:
  - `1742, 1993, 2663, 2785, 2932, 3080, 3309, 3387, 3433, 3629, 4060, 3977, 3946, 3469, 3974, 4035`.
- `JPEG13_SIZE` должен быть `3979`, но восстановлено `3469`: ровно минус один `510`-byte payload chunk из-за одиночного `XACT_ERROR` в frame `4467`. `SOI/EOI` при этом есть.
- Последний кадр перед финальным срывом: `JPEG15`, восстановлен полностью `4035/4035`, `SOI/EOI` есть.
- Следующий ожидаемый кадр `JPEG16_SIZE = 4119`; его `SOI` в pcap уже не появляется.

Снимок watch variables после срыва:

- `usbx_video_stream_task_status_dbg = 0`.
- `usbx_video_stream_buffer_error_count_dbg = 0`.
- `usbx_video_get_status_dbg = 0`.
- `usbx_video_commit_status_dbg = 0`.
- `usbx_video_write_calls_dbg = 112`.
- `usbx_video_payload_done_dbg = 110`.
- `usbx_dcd_run_tx_calls_dbg = 127`.
- `usbx_dcd_run_tx_status_dbg = 0`.
- `usbx_dcd_data_in_calls_dbg = 138`.
- `usbx_dcd_iso_incomplete_calls_dbg = 0`.

Вывод по этому прогону:

- Это не запись нулевого JPEG: последний MJPEG кадр полный, а следующий MJPEG кадр в pcap вообще не начинается.
- Срыв снова происходит на границе после завершенного кадра, но не на том же JPEG index, что в предыдущем pcap:
  - прошлый срыв был после полного `JPEG10`;
  - текущий срыв после полного `JPEG15`.
- Если сравнивать фазу, место похоже: переход от завершенного кадра к следующему payload/frame.
- USBX-level counters не показывают ошибку очереди (`get/commit/status/buffer_error = 0`), а DCD transmit status тоже `0`.
- `usbx_dcd_iso_incomplete_calls_dbg = 0` при наличии host-side `XACT_ERROR` в pcap означает, что текущая прошивка, вероятно, не видит ISO incomplete interrupt path. Это совместимо с маскированием `IISOIXFRM`.

Следующее диагностическое действие:

- Для следующего прогона не менять UVC descriptors/probe.
- Развести два вопроса:
  - "USBX не ставит следующий payload" - ловить через `write_calls/payload_done/transfer_len/access_len`;
  - "DCD/HAL поставил transfer, но OTG/host получил XACT" - ловить через EP1 registers/FIFO и немаскированный `IISOIXFR`.
- Диагностически включить видимость `IISOIXFR`/`ISOINIncomplete` и логировать первый момент, когда pcap получает `XACT_ERROR`, но firmware still reports `HAL_OK`.

## 2026-04-30: masked IISOIXFR polling diagnostic

Изменение:

- UVC descriptors/probe не менялись.
- `IISOIXFRM` остается замаскированным, чтобы не возвращать старый interrupt storm / HAL abort path.
- Добавлен polling sticky-бита `USB_OTG_HS->GINTSTS & USB_OTG_GINTSTS_IISOIXFR` в `MX_USBX_Device_Process()`.
- Если bit замечен, firmware сохраняет snapshot EP1 IN registers и очищает только sticky `IISOIXFR` bit.

Новые watch variables:

- `usbx_iisoixfr_poll_count_dbg`;
- `usbx_iisoixfr_poll_first_gintsts_dbg`;
- `usbx_iisoixfr_poll_first_gintmsk_dbg`;
- `usbx_iisoixfr_poll_first_dsts_dbg`;
- `usbx_iisoixfr_poll_first_diepctl_dbg`;
- `usbx_iisoixfr_poll_first_dieptsiz_dbg`;
- `usbx_iisoixfr_poll_first_diepint_dbg`;
- `usbx_iisoixfr_poll_first_dtxfsts_dbg`;
- `usbx_iisoixfr_poll_last_gintsts_dbg`;
- `usbx_iisoixfr_poll_last_gintmsk_dbg`;
- `usbx_iisoixfr_poll_last_dsts_dbg`;
- `usbx_iisoixfr_poll_last_diepctl_dbg`;
- `usbx_iisoixfr_poll_last_dieptsiz_dbg`;
- `usbx_iisoixfr_poll_last_diepint_dbg`;
- `usbx_iisoixfr_poll_last_dtxfsts_dbg`.

Как читать следующий прогон:

- Если pcap показывает `XACT_ERROR`, а `usbx_iisoixfr_poll_count_dbg` растет, OTG core сам сообщает incomplete, но interrupt path скрыт маской. Тогда дальше смотреть `DIEPCTL/DIEPTSIZ/DTXFSTS/DIEPINT` на первом snapshot.
- Если pcap показывает `XACT_ERROR`, а `usbx_iisoixfr_poll_count_dbg = 0`, значит host-side XACT не сопровождается sticky `IISOIXFR`; тогда подозрение смещается к тому, что EP не был вооружен вовремя или USBX/DCD state machine остановилась до программирования transfer.
- Если `first_dieptsiz` содержит активный `XFRSIZ/PKTCNT`, а `DIEPCTL.EPENA` выставлен, payload был поставлен в OTG на момент incomplete.
- Если `first_dieptsiz = 0` или `DIEPCTL.EPENA = 0`, надо копать, почему следующий payload не был armed после полного EOF кадра.

Сборка:

- `make -C Debug all -j8` проходит успешно.
- Размер: `text=201904`, `data=456`, `bss=491888`.

## 2026-04-30 19:49:01: pcap после masked IISOIXFR polling

Файл `C:\Users\Professional\Documents\UVC.pcapng`:

- Обновлен `2026-04-30 19:49:01`, размер `19753848` bytes.
- Устройство в захвате: USB address `45`.
- `SET_INTERFACE alt=1`: frame `2587`, время `1.368502 s`.
- `SET_INTERFACE alt=0`: frame `31981`, время `10.379370 s`.
- EP `0x81`, ISO IN rows с `128` descriptors: `10790`.
- Ненулевые ISO IN URB: `27`.
- Первый payload: frame `2649`, `1.390321 s`, `data_len = 1024`.
- Последний payload: frame `6921`, `3.214315 s`, `data_len = 737`, `iso_error_count = 112`.
- Следующий URB: frame `6951`, `3.230314 s`, `data_len = 0`, `USBD_STATUS_ISOCH_REQUEST_FAILED`, `iso_error_count = 128`.
- После frame `6951` payload уже не восстанавливается до `SET_INTERFACE alt=0`.

Реконструкция MJPEG/UVC payload:

- Восстановлено `22` завершенных кадров.
- Последовательность размеров совпадает с `JPEG0..JPEG21` из `USBX/Stream/stream1.h`:
  - `1742, 1993, 2663, 2785, 2932, 3080, 3309, 3387, 3433, 3629, 4060, 3977, 3946, 3979, 3974, 4035, 4119, 4312, 4282, 4292, 4298, 4303`.
- В этом прогоне до финального срыва нет ранних одиночных payload losses: все `JPEG0..JPEG21` полные, `SOI/EOI` есть.
- Последний кадр перед срывом: `JPEG21`, восстановлен полностью `4303/4303`.
- Следующий ожидаемый кадр `JPEG22_SIZE = 4305`; его `SOI` в pcap не появляется.
- Финальный URB frame `6921` содержит два последних payload chunk кадра `JPEG21`:
  - `hdr=0200`, payload `510`;
  - `hdr=0202`, EOF payload `223`;
  - после EOF в том же URB остаются `112` host-side `XACT_ERROR`.

Снимок watch variables после срыва:

- `usbx_video_stream_task_status_dbg = 0`.
- `usbx_video_stream_buffer_error_count_dbg = 0`.
- `usbx_video_get_status_dbg = 0`.
- `usbx_video_commit_status_dbg = 0`.
- `usbx_video_write_calls_dbg = 166`.
- `usbx_video_payload_done_dbg = 164`.
- `usbx_dcd_run_tx_calls_dbg = 181`.
- `usbx_dcd_run_tx_status_dbg = 0`.
- `usbx_dcd_data_in_calls_dbg = 192`.
- `usbx_dcd_iso_incomplete_calls_dbg = 0`.
- `usbx_iisoixfr_poll_count_dbg = 50417`.

Вывод по этому прогону:

- `payload_done_dbg = 164` точно совпадает с числом успешно восстановленных UVC payload packets в pcap.
- `write_calls_dbg = 166` означает, что после последнего successful payload в USBX queue остаются два подготовленных payload. Это не похоже на остановку producer/app layer.
- `get/commit/status/buffer_error = 0`, значит USBX video queue не сообщает ошибку.
- `dcd_run_tx_status_dbg = 0`, значит DCD/HAL path не возвращает ошибку при постановке transfer.
- `usbx_iisoixfr_poll_count_dbg = 50417` подтверждает, что OTG core действительно видит ISO IN incomplete events даже при замаскированном `IISOIXFRM`.
- `usbx_dcd_iso_incomplete_calls_dbg = 0` теперь объясняется маской interrupt: callback не вызывается, но sticky bit виден polling-диагностикой.
- Это сильнее смещает подозрение с UVC payload generation/descriptors на OTG ISO IN scheduling / endpoint frame parity / Tx FIFO / HAL low-level state after masked incomplete.

Следующее, что нужно снять в watch после такого срыва:

- `usbx_iisoixfr_poll_first_diepctl_dbg`;
- `usbx_iisoixfr_poll_first_dieptsiz_dbg`;
- `usbx_iisoixfr_poll_first_diepint_dbg`;
- `usbx_iisoixfr_poll_first_dtxfsts_dbg`;
- `usbx_iisoixfr_poll_last_diepctl_dbg`;
- `usbx_iisoixfr_poll_last_dieptsiz_dbg`;
- `usbx_iisoixfr_poll_last_diepint_dbg`;
- `usbx_iisoixfr_poll_last_dtxfsts_dbg`.

Интерпретация:

- Если `DIEPCTL.EPENA = 1` и `DIEPTSIZ` содержит ненулевые `XFRSIZ/PKTCNT`, transfer был armed, но не дошел до host.
- Если `DIEPCTL.EPENA = 0` или `DIEPTSIZ = 0`, очередь USBX содержит payload, но EP1 не armed в момент incomplete.

## 2026-04-30 20:29:24: no-delay experiment признан ошибочным

Файл `C:\Users\Professional\Documents\UVC.pcapng`:

- Обновлен `2026-04-30 20:29:24`, размер `12217588` bytes.
- Устройство в захвате: USB address `41`.
- `SET_INTERFACE`: frames `1275` (`0.829977 s`) и `4413` (`2.934950 s`).
- EP `0x81` после второго `SET_INTERFACE`: `7804` ISO IN rows.
- Ненулевые ISO IN URB: `114`.
- URB с `iso_error_count > 0`: `1260`.
- Последний ненулевой payload: frame `7223`, `4.764316 s`, `data_len = 3732`, `iso_error_count = 0`.
- Первый полный отказ: frame `7251`, `4.780300 s`, `data_len = 0`, `iso_error_count = 128`.
- Сразу после этого host делает `URB_FUNCTION_ABORT_PIPE`, дальше идут серии `data_len = 0` и повторные `iso_error_count = 128`.

Вывод:

- Удаление `ux_utility_delay_ms(USBD_VIDEO_GetFramePeriodMs())` было ошибкой. Без паузы поток может сорваться уже на первом кадре или быстро перейти в поток header-only/payload bursts.
- Откатить no-delay/header-only pacing и оставить исходную frame pause.
- Несмотря на ошибочность эксперимента, ring trace полезен: в момент отказа есть `WAIT` при активном transfer request и затем плотный `IISO`.
- Снимок регистров из ring trace:
  - `DIEPCTL = 0x80448200`: EP1 IN enabled, ISO, MPS 512, TxFIFO 1.
  - `DIEPTSIZ = 0x20080000`: PKTCNT/MULCNT выставлены, `XFRSIZ = 0`.
  - `DIEPINT = 0x2090`: `ITTXFE`, `TXFE`, `NAK`; `XFRC = 0`.
  - `DTXFSTS = 0x280`.
- Это классифицирует отказ как `SUBMIT -> IISO -> no DATI/XFRC`, ближе к low-level ISO scheduling/FIFO/odd-even/recovery, а не к producer/cache/ownership.

Изменение после этого прогона:

- В `ux_device_video.c` возвращена пауза `ux_utility_delay_ms(USBD_VIDEO_GetFramePeriodMs())` перед commit EOF packet.
- Убран экспериментальный header-only pacing (`VIDL`, `video_next_frame_tick`, `usbx_video_header_only_dbg`, `usbx_video_wait_frame_dbg`).
- `IISO` ring logging больше не считает изменение только `DSTS` новым событием; иначе буфер забивается каждым микрофреймом и теряет события перед отказом.
- Периодический forced snapshot `IISO` оставлен раз в `4096` polls.

## 2026-04-30 20:52:06: USBX stuck in WAIT после masked IISO

Новый ring trace:

- Ring заполнен `WAIT`, редкие `IISO` snapshots.
- `WAIT`: `a0 = 0x81`, `a1 = 0x200`, `a2 = 0x3`.
  - EP `0x81`;
  - requested length `512`;
  - ED status `0x3 = USED | TRANSFER`;
  - `DONE` отсутствует.
- `WAIT.a4 = 0x1ce`, то есть `done_seen = 462`.
- `IISO`: `payload_done = 0x1ce`, `write_calls = 0x1d0`, `suppressed = 0xfff`.
- `DIEPCTL = 0x80448200`, `DIEPTSIZ = 0x20080000`, `DIEPINT = 0x2090`, `DTXFSTS = 0x280`.
- `usbx_iisoixfr_poll_count_dbg = 135667`.

Вывод:

- Это уже не отсутствие producer data: приложение подготовило больше payload, чем было завершено.
- USBX/DCD застрял на одном EP1 transfer: `TRANSFER` есть, `DONE` не появляется.
- Так как `IISOIXFRM` замаскирован, HAL callback не вызывается; polling видел sticky `IISOIXFR`, но до этой правки только очищал bit и не освобождал ED.

Изменение:

- Добавлен masked-IISO recovery path в `USBX_PollMaskedIISOIXFR()`.
- Если EP1 ED находится в `TRANSFER` без `DONE`, текущий ISO packet считается потерянным:
  - `HAL_PCD_EP_Abort(0x81)`;
  - `USB_FlushTxFifo(EP1)`;
  - current `UX_SLAVE_TRANSFER` помечается `UX_SUCCESS`, `UX_TRANSFER_STATUS_COMPLETED`, `actual_length = 0`;
  - ED получает `DONE`, чтобы `_ux_dcd_stm32_transfer_run()` вернул `UX_STATE_NEXT`;
  - UVC class освобождает текущий payload и ставит следующий.
- Добавлен trace event `IREC` (`0x49524543`) и watch-переменные:
  - `usbx_iisoixfr_recovery_enable_dbg`;
  - `usbx_iisoixfr_recovery_calls_dbg`;
  - `usbx_iisoixfr_recovery_skip_dbg`;
  - `usbx_iisoixfr_recovery_abort_status_dbg`;
  - `usbx_iisoixfr_recovery_flush_status_dbg`;
  - `usbx_iisoixfr_recovery_ed_status_dbg`;
  - `usbx_iisoixfr_recovery_transfer_status_dbg`;
  - `usbx_iisoixfr_recovery_req_len_dbg`;
  - `usbx_iisoixfr_recovery_diepctl_dbg`;
  - `usbx_iisoixfr_recovery_dieptsiz_dbg`;
  - `usbx_iisoixfr_recovery_diepint_dbg`;
  - `usbx_iisoixfr_recovery_dtxfsts_dbg`.
- `WAIT` trace разрежен с каждого `0x400` до каждого `0x4000` wait-loop, чтобы кольцо сохраняло больше событий восстановления.

Ожидаемая интерпретация следующего прогона:

- Если `IREC` появляется и после него идут `DONE -> VDON -> VCOM/SUBM`, recovery освобождает USBX state machine.
- Если `IREC` появляется постоянно, но payload на host не восстанавливается, проблема ниже: abort/flush не возвращают EP1 в рабочее состояние или следующий `USB_EPStartXfer` программирует неверный frame parity.
- Если `usbx_iisoixfr_recovery_abort_status_dbg != 0`, смотреть, не зависает ли `EPENA` при abort.

## 2026-04-30 21:02:51: recovery unblocks USBX, but stream falls into IISO storm

Fresh USBPcap:

- File `C:\Users\Professional\Documents\UVC.pcapng`, size `23894692` bytes, duration `25.705 s`.
- UVC stream is `bus 1 / device 1 / EP 0x81`.
- Last non-zero EP81 URB: frame `27576`, `18.760838 s`, `Packet Data Length = 627`.
- That last non-zero URB is already damaged: `Isochronous transfer error count = 71`.
- Next EP81 URB: frame `27593`, `18.776836 s`, `Packet Data Length = 0`, `Isochronous transfer error count = 128`; all ISO descriptors are `USBD_STATUS_XACT_ERROR`.
- No `SET_INTERFACE` or stream reset follows; host keeps scheduling EP81 IN, but device produces no valid ISO transactions.

Firmware ring from same run:

- `IREC` works mechanically: `abort_status = 0`, `flush_status = 0`.
- After `IREC`, `_ux_dcd_stm32_transfer_run()` sees `DONE`, callback `VDON` runs with `length = 0`, and video immediately commits the next payload.
- Then the next `SUBM` is followed by another `IISO -> IREC`, repeatedly.

Interpretation:

- The first recovery fixed the USBX busy-state deadlock, but it did not resynchronize the video producer/frame boundary.
- A recovered ISO packet with `actual_length = 0` was treated like a normal completed payload, so UVC kept advancing inside a damaged frame and resubmitting immediately.
- This matches a low-level ISO phase/scheduling miss plus missing class-level resync, not a JPEG producer/cache ownership failure.

Change:

- Added `usbx_video_iso_recovery_pending_dbg`.
- `IREC` increments this flag.
- `USBD_VIDEO_StreamPayloadDone()` now treats `length == 0 && recovery_pending != 0` as frame damage:
  - logs `VRSY`;
  - resets `video_packet_index` to `0`;
  - waits one `USBD_VIDEO_GetFramePeriodMs()` before preparing the next payload.
- Added watch variables:
  - `usbx_video_iso_recovery_pending_dbg`;
  - `usbx_video_resync_count_dbg`;
  - `usbx_video_resync_delay_ms_dbg`.

Next run:

- If recovery is correct, trace should show occasional `IISO -> IREC -> DONE -> VDON(0) -> VRSY -> VCOM/SUBM`, followed by valid `DATI` again.
- If it becomes `IISO -> IREC -> VRSY` forever, the remaining suspect is STM32 ISO odd/even frame scheduling or HAL re-arm timing after abort/flush.

## 2026-04-30 21:10:49: VRSY works, but every next SUBM still misses

Fresh USBPcap:

- File `C:\Users\Professional\Documents\UVC.pcapng`, size `14801704` bytes, duration `8.820616 s`.
- UVC stream is `bus 1 / device 60 / EP 0x81`.
- Last non-zero EP81 URB: frame `5161`, `3.406271 s`, `data_len = 1536`.
- After frame `5187` / `3.422260 s`, EP81 remains scheduled but payload is zero.

Firmware ring:

- `usbx_iisoixfr_recovery_calls_dbg = 0x2e`.
- `usbx_video_resync_count_dbg = 0x2e`.
- `usbx_video_iso_recovery_pending_dbg = 0`.
- The ring is a stable repeated pattern:
  - `VCOM -> SUBM -> IISO -> IREC -> DONE -> VDON(length=0) -> VRSY`.
- `VRSY.a5 = 0x50`, so the class-level resync waits one full frame period.
- The next `SUBM` still immediately produces `IISO`.

Interpretation:

- Video/class resync is confirmed working but irrelevant to the root cause.
- The remaining fault is below UVC: ISO IN is being armed in a frame/microframe where the controller does not transmit.
- `SUBM` snapshots still show `DIEPINT = 0x2090`, `DIEPTSIZ = 0x20080000`, `DTXFSTS = 0x280`, matching `SUBM -> IISO -> no XFRC/DATI`.

Change:

- Added low-level ISO scheduling experiment in `USB_EPStartXfer()` for EP1 ISO IN:
  - `usb_ll_iso_schedule_same_parity_dbg = 1` schedules the same odd/even parity as the current `DSTS.FNSOF` bit, giving the controller one extra microframe instead of targeting the nearest opposite parity.
  - `usb_ll_iso_write_before_enable_dbg = 1` writes the packet FIFO before setting `EPENA`.
- Added watch variables:
  - `usb_ll_iso_target_dsts_dbg`;
  - `usb_ll_iso_target_odd_dbg`;
  - `usb_ll_iso_write_pre_enable_used_dbg`;
  - existing `usb_ll_iso_*` and `usb_ll_writepacket_*` variables remain useful.

## 2026-04-30: same-parity scheduling breaks stream start

User run after the low-level experiment:

- Stream did not start.
- `usbx_iisoixfr_recovery_calls_dbg = 0x2d`.
- `usbx_video_resync_count_dbg = 0x2d`.
- `usb_ll_iso_schedule_same_parity_dbg = 1`.
- `usb_ll_iso_write_before_enable_dbg = 1`.
- `usb_ll_iso_write_pre_enable_used_dbg = 0x34`.
- `usb_ll_iso_dtxfsts_before_write_dbg = 0x300`.
- `usb_ll_iso_dtxfsts_after_write_dbg = 0x280`.
- `usb_ll_iso_diepctl_after_enable_dbg = 0x80458200`.

Interpretation:

- FIFO pre-write is real: `DTXFSTS 0x300 -> 0x280` means one 512-byte packet was written before endpoint enable.
- Same-parity scheduling is a bad default on this controller/path: after the change the firmware immediately repeats `SUBM -> IREC -> DONE(0) -> VRSY`, with no `DATI`.
- Therefore the next isolated test is old/opposite parity plus pre-write FIFO.

Change:

- `usb_ll_iso_schedule_same_parity_dbg` default changed back to `0`.
- `usb_ll_iso_write_before_enable_dbg` remains `1`.

Next run:

- If the stream starts again and lives longer, FIFO-empty-at-enable was likely part of the failure, while same-parity scheduling was wrong.
- If the stream still does not start, disable pre-write at runtime with `set var usb_ll_iso_write_before_enable_dbg=0` before starting the stream to return to the previous low-level order without reflashing.

## 2026-04-30 21:24:12: pre-write also prevents stream start

Fresh USBPcap:

- File `C:\Users\Professional\Documents\UVC.pcapng`, size `4807204` bytes.
- UVC device is USB address `9`.
- `SET_INTERFACE alt=1`: frame `1981`, `1.292909 s`.
- `SET_INTERFACE alt=0`: frame `7047`, `4.307203 s`.
- Between alt=1 and alt=0, EP `0x81` is polled, but all captured rows for this device have `data_len = 0`.
- For device `9`, EP `0x81` summary is `380` rows, all zero data.

Interpretation:

- The old/opposite parity plus FIFO pre-write still does not start the stream.
- Therefore pre-write-before-EPENA is not a safe default on this OTG HS device path, even though `DTXFSTS` proved that the packet was written into FIFO.
- The earlier "stream starts then later freezes" behavior is preferable for diagnosis because it gives successful `DATI/XFRC` before the failure. We should return the low-level transmit ordering to the previous default before trying another recovery idea.

Change:

- `usb_ll_iso_schedule_same_parity_dbg = 0`.
- `usb_ll_iso_write_before_enable_dbg = 0`.
- The diagnostic code remains in place so both experiments can still be enabled from GDB if needed.

Next run:

- Confirm that the stream starts again with both low-level experiments disabled.
- If it returns to "runs then freezes", continue debugging the first `SUBM -> IISO -> no DATI/XFRC` transition using the ring trace and EP1 register snapshots.

## 2026-04-30: flush USBX payload queue on ISO recovery resync

Reasoning:

- `IISOIXFR` recovery marked the active transfer as completed with `actual_length = 0`.
- `USBD_VIDEO_StreamPayloadDone()` then reset `video_packet_index = 0`, so the next generated payload started a new UVC frame and toggled `FID`.
- But USBX can already have committed payload buffers ahead of the active transfer (`write_calls > payload_done` was observed).
- Those queued payloads may belong to the damaged old frame. If they are sent after recovery, the host can see stale frame tail before the new clean frame/FID and fail to resync.

Change:

- Added `USBD_VIDEO_FlushPayloadQueue()` and call it from the `VRSY` recovery path before generating the next payload.
- The helper clears all USBX video payload ring entries:
  - every `ux_device_class_video_payload_length = 0`;
  - payload data bytes zeroed;
  - `transfer_pos` reset to `access_pos`, matching the USBX alt-setting reset pattern.
- Added watch variables:
  - `usbx_video_payload_flush_count_dbg`;
  - `usbx_video_payload_flush_slots_dbg`;
  - `usbx_video_payload_flush_nonzero_dbg`;
  - `usbx_video_payload_flush_bytes_dbg`;
  - `usbx_video_payload_flush_transfer_pos_dbg`;
  - `usbx_video_payload_flush_access_pos_dbg`.
- `VRSY` trace now uses:
  - `a9 = usbx_video_payload_flush_nonzero_dbg`;
  - `a10 = usbx_video_stream_transfer_len_dbg`;
  - `a11 = usbx_video_stream_access_len_dbg`.

Expected next run:

- On the first recovery, `usbx_video_payload_flush_nonzero_dbg` should show whether stale committed payloads existed.
- After `VRSY`, `transfer_len/access_len` should be `0` before the new payload commit.
- If the stream still enters `SUBM -> IISO -> IREC -> VRSY` forever, stale USBX queued payloads were not the root cause; the remaining suspect stays low-level ISO arm/schedule/FIFO after abort/flush.

## 2026-04-30 22:00:56: recovery can return data, but FID is not clean across gaps

Fresh USBPcap:

- File `C:\Users\Professional\Documents\UVC.pcapng`, size `18954136` bytes.
- UVC device is USB address `3`.
- `SET_INTERFACE alt=1`: frame `2197`, `1.420831 s`.
- EP `0x81`: `10406` ISO rows, `101` non-zero rows, `10305` zero rows.
- First non-zero ISO data: frame `2317`, `1.506278 s`.
- Last non-zero ISO data: frame `18539`, `12.578268 s`, `data_len = 4310`.
- After frame `18539`, EP81 continues with `data_len = 0` until the end of capture at `18.508553 s`.

Non-zero ISO data arrives in five blocks:

- `1.506278..2.578287 s`, `15` URBs, `38410` bytes.
- `3.618327..5.298298 s`, `25` URBs, `77140` bytes.
- `6.498320..8.402274 s`, `25` URBs, `71310` bytes.
- `9.042294..10.786281 s`, `25` URBs, `69881` bytes.
- `11.906271..12.578268 s`, `11` URBs, `36659` bytes.

Firmware counters after the same run:

- `usbx_video_payload_flush_count_dbg = 0x91` (`145`).
- `usbx_video_payload_flush_slots_dbg = 0x8`.
- `usbx_video_payload_flush_nonzero_dbg = 0`.
- `usbx_video_payload_flush_bytes_dbg = 0`.
- `transfer_pos == access_pos == 0xd004c238`.

Interpretation:

- The USBX software payload queue was empty at every flush point, so stale committed software payloads are not the primary explanation.
- The stream did recover several times, but pcap reconstruction shows bad UVC frame boundaries after some gaps:
  - partial frames with SOI but no EOF/EOI;
  - sizes not present in `stream1.h`, for example `7516`, `4193`, `6853`;
  - at least one merged frame spans a long no-data gap with the same FID.
- This means the remaining issue is not just "no payload"; recovery can return data, but the next visible UVC frame boundary/FID is sometimes wrong.

Likely mechanism:

- `FID` was toggled when a payload was committed, not when a packet was known to have completed on the bus.
- If the first packet of a new UVC frame was committed and then lost, recovery reset `video_packet_index = 0`; the next payload toggled `FID` again.
- Depending on whether the host saw any packet of the damaged frame, this can either repeat the previous visible FID or merge a partial old frame with the next frame.

Change:

- Replaced function-local static `fid` with explicit stream state `video_fid`.
- Added completed-packet tracking:
  - `video_done_packets_in_frame`;
  - `usbx_video_done_packets_in_frame_dbg`;
  - `usbx_video_last_done_header_dbg`.
- On `VDON(length > 0)`, the callback reads the header byte from the just-completed payload slot and counts packets completed since the last EOF.
- On `VRSY`:
  - if packets from the damaged frame were completed, next FID is forced to the opposite of the last completed FID;
  - if no packet from the damaged frame completed, next FID reuses the current committed FID instead of toggling twice.
- Added watch variables:
  - `usbx_video_fid_dbg`;
  - `usbx_video_resync_next_fid_dbg`;
  - `usbx_video_resync_fid_action_dbg` (`1` = retry same FID, `2` = opposite FID after partial frame);
  - `usbx_video_resync_seen_packets_dbg`;
  - `usbx_video_done_packets_in_frame_dbg`;
  - `usbx_video_last_done_header_dbg`.
- `VRSY` trace now uses:
  - `a9 = usbx_video_resync_fid_action_dbg`;
  - `a10 = usbx_video_resync_next_fid_dbg`;
  - `a11 = packets seen before recovery`.

Next run:

- Check if pcap no longer has completed frames with repeated visible FID after recovery.
- Check if large merged sizes across recovery gaps disappear.
- If the stream still eventually hangs with clean FID boundaries, return focus to low-level EP1 ISO re-arm after `HAL_PCD_EP_Abort + USB_FlushTxFifo`.

## 2026-04-30 22:20:05: FID change did not fix final IISO loop; EOF was delayed inside frame

Fresh USBPcap:

- File `C:\Users\Professional\Documents\UVC.pcapng`, size `19082124` bytes.
- UVC device is USB address `15`.
- `SET_INTERFACE alt=1`: frame `2470`, `1.395644 s`.
- EP `0x81`: `10574` ISO rows, `88` non-zero rows, `252279` non-zero bytes.
- First non-zero ISO data: frame `2632`, `1.481126 s`.
- Last non-zero ISO data: frame `17388`, `11.625139 s`, `data_len = 2969`.

Non-zero ISO data arrives in five blocks:

- `1.481126..1.561149 s`, `2` URBs, `3286` bytes.
- `2.681146..4.425124 s`, `21` URBs, `60057` bytes.
- `5.465127..7.785137 s`, `33` URBs, `94298` bytes.
- `8.665214..10.745158 s`, `30` URBs, `88085` bytes.
- `11.545117..11.625139 s`, `2` URBs, `6553` bytes.

Firmware trace:

- The final trace is still a repeated low-level loop:
  - `VCOM -> SUBM -> IISO/IREC -> DONE -> VDON(length=0) -> VRSY`.
- `VRSY.a9 = 1`, `a10 = 1`, `a11 = 0`:
  - recovery selected "same FID";
  - no completed packet was seen in the damaged frame at the time of recovery.
- Therefore the explicit FID state did not change the final failure mode.

UVC reconstruction:

- There are still partial frames with SOI but no EOF/EOI:
  - sizes such as `1530`, `3060`, `3570`, `2550`, `4080`.
- These sizes are multiples or near-multiples of the 510-byte payload capacity.
- This points to an application-level pacing bug: the last packet of a frame was being delayed before it was committed.

Root of the damaged-frame side effect:

- `ux_utility_delay_ms(USBD_VIDEO_GetFramePeriodMs())` was inside the EOF-packet generation branch, before `ux_device_class_video_write_payload_commit()`.
- That means the host could receive SOI and most of the JPEG, then wait one frame period before the EOF packet was even queued.
- If ISO recovery happened during that wait, the current frame was dropped without EOF, producing exactly the partial frames seen in pcap.

Change:

- Moved frame pacing out of the EOF commit path.
- EOF packet is now committed immediately.
- A new `video_frame_delay_pending` flag delays only before the first payload of the next frame, after the previous EOF has completed.
- Added watch variables:
  - `usbx_video_frame_delay_pending_dbg`;
  - `usbx_video_frame_delay_count_dbg`.

Expected next run:

- Partial frames ending just before EOF should drop sharply or disappear.
- If stream still dies, the final loop is still low-level EP1 ISO scheduling/recovery, not UVC FID or JPEG frame pacing.

## 2026-04-30 22:39:00: EOF pacing improved frames; final failure still leaves EP1 interrupt bits latched

Fresh USBPcap:

- File `C:\Users\Professional\Documents\UVC.pcapng`, size `13364024` bytes.
- UVC device is USB address `17`.
- `SET_INTERFACE alt=1`: frame `1723`, `1.218471 s`.
- EP `0x81`: `8150` ISO URBs, `34` URBs with data, `1242` URBs with ISO errors.
- Reconstructed UVC frames:
  - `29` frame attempts;
  - `23` complete frames with SOI/EOI/EOF;
  - `6` partial frames without host-visible EOF/EOI.
- Firmware counters match this direction:
  - `usbx_video_frame_delay_count_dbg = 0x17` (`23`);
  - `usbx_video_frame_eof_dbg = 0x1d` (`29`);
  - `usbx_video_payload_done_dbg = 0x151`;
  - `usbx_video_resync_count_dbg = 0x74`.

Conclusion:

- Moving the frame delay after EOF commit reduced the damaged-frame symptom substantially.
- The final failure mode did not change:
  - last host-visible frame is complete;
  - after that `data_len = 0`;
  - host sees repeated ISO `XACT_ERROR`;
  - firmware trace loops through `VRSY -> VCOM/SUBM -> IISO/IREC`.

New low-level clue:

- Every failed `SUBM` after recovery still shows `DIEPINT = 0x2090`.
- On STM32H7 OTG this is:
  - `NAK`;
  - `TXFE`;
  - `ITTXFE` (`IN token received when TxFIFO is empty`).
- Because `IISOIXFR` is masked and polled manually, the normal HAL IRQ path that clears IN EP interrupt bits is bypassed.

Change:

- In `USBX_RecoverMaskedIISOIXFR()` after `HAL_PCD_EP_Abort()` and `USB_FlushTxFifo()`:
  - clear `hpcd_USB_OTG_HS.IN_ep[1].is_iso_incomplete`;
  - clear EP1 from `DIEPEMPMSK`;
  - clear latched EP1 `DIEPINT` bits: `EPDISD`, `TOC`, `ITTXFE`, `INEPNM`, `INEPNE`, `TXFE`, `PKTDRPSTS`, `NAK`.
- Added watch variables:
  - `usbx_iisoixfr_recovery_diepint_after_abort_dbg`;
  - `usbx_iisoixfr_recovery_diepint_after_clear_dbg`;
  - `usbx_iisoixfr_recovery_iso_flag_dbg`.

Next run:

- If this is the right missing cleanup, `SUBM.a8` should stop carrying stale `0x2090` after an `IREC`.
- If `SUBM.a8` becomes clean but every next packet still gets `IISO`, endpoint abort/flush is not enough and the next experiment should be a harder EP1 reopen/reset after recovery.

## 2026-04-30 22:55:00: DIEPINT cleanup changed symptoms but caused recovery loop

Fresh trace after the explicit `DIEPINT` cleanup:

- `usbx_iisoixfr_recovery_calls_dbg = 0x15b`.
- `usbx_video_resync_count_dbg = 0x15b`.
- `usbx_iisoixfr_recovery_diepint_dbg = 0x80`.
- `usbx_iisoixfr_recovery_diepint_after_abort_dbg = 0xc0`.
- `usbx_iisoixfr_recovery_diepint_after_clear_dbg = 0x80`.
- `usbx_iisoixfr_recovery_iso_flag_dbg = 0`.

Conclusion:

- The cleanup removed the old `0x2090` pattern from the submit path, but it did not restore stable scheduling.
- The stream now repeatedly does `VRSY -> VCOM/SUBM -> IISO/IREC` with no `DATI` between attempts.
- `TXFE` (`0x80`) alone is not a convincing root cause; the remaining suspect is still the first ISO IN arm after recovery.

Change:

- `DIEPINT` clearing is now gated by `usbx_iisoixfr_recovery_clear_diepint_enable_dbg`, default `0`.
- Recovery now sets `usb_ll_iso_after_recovery_dbg = 1`.
- `USB_EPStartXfer()` consumes that flag only for the next EP1 ISO IN packet with non-zero length.
- Added one-shot recovery mode:
  - `usb_ll_iso_recovery_mode_dbg = 1` by default: schedule the same odd/even parity only for the first submit after recovery.
  - bit `1`: same-parity scheduling after recovery.
  - bit `2`: write FIFO before `EPENA` after recovery.
  - `usb_ll_iso_recovery_mode_used_dbg` records the last consumed mode.
  - `usb_ll_iso_recovery_mode_consumed_dbg` counts consumed recovery submits.

Next run:

- If this is a recovery-only odd/even issue, the first packet after `VRSY` should stop going immediately to `IISO`.
- If it still loops, try `set var usb_ll_iso_recovery_mode_dbg=2` before starting the stream to test recovery-only FIFO prewrite without same-parity.

## 2026-04-30 23:05:00: recovery-only same parity keeps device alive, but VRSY delay dominates

Fresh USBPcap after one-shot recovery same-parity:

- File `C:\Users\Professional\Documents\UVC.pcapng`, size `48983908` bytes.
- UVC stream is USB address `29`, EP `0x81`.
- Last non-zero EP81 data is at `58.787956 s`, so the device no longer dies quickly.
- EP81 summary:
  - `24736` URBs;
  - `141` URBs with data;
  - `2927` URBs with ISO errors;
  - `142945` data bytes.
- Data arrives in `35` separated islands.
- Inter-island gaps:
  - average about `1.4 s`;
  - maximum `2.656 s`.

UVC reconstruction:

- `283` host-visible UVC payload packets.
- `132` frame attempts.
- Only `7` complete JPEG+EOF frames.
- `125` partial attempts, mostly:
  - `510` bytes;
  - `1020` bytes;
  - `1530` bytes;
  - `2040` bytes.

Interpretation:

- Recovery-only same-parity is directionally useful: the device can return data for almost a minute.
- But recovery is still frequent, usually after only the first few UVC payload packets.
- The class recovery path currently waits one full UVC frame period (`0x50 ms`) on every `VRSY`.
- Multiple consecutive low-level misses therefore turn into visible `1..2.6 s` stalls.

Change:

- Added `usbx_video_resync_delay_cfg_ms_dbg`, default `1`.
- `VRSY` now uses this short configurable delay instead of `USBD_VIDEO_GetFramePeriodMs()`.
- The normal clean-frame pacing after EOF is unchanged.
- `usbx_video_resync_delay_ms_dbg` still records the actual VRSY delay used.

Next run:

- If the same low-level misses continue, `resync_count` may still grow, but visual recovery should be much faster.
- If immediate resubmission makes the low-level loop worse, set `set var usbx_video_resync_delay_cfg_ms_dbg=0x50` to restore the old full-frame delay without reflashing.

## 2026-04-30 23:12:00: same-parity after recovery is consumed, but does not reduce miss rate

Firmware counters after the recovery-only same-parity run:

- `usb_ll_iso_after_recovery_dbg = 1`.
- `usb_ll_iso_recovery_mode_dbg = 1`.
- `usb_ll_iso_recovery_mode_used_dbg = 1`.
- `usb_ll_iso_recovery_mode_consumed_dbg = 0x371`.
- `usbx_iisoixfr_recovery_calls_dbg = 0x372`.
- `usbx_video_resync_count_dbg = 0x372`.
- `usb_ll_iso_write_pre_enable_used_dbg = 0`.
- `usbx_iisoixfr_recovery_clear_diepint_enable_dbg = 0`.

Interpretation:

- The one-shot hook is working: nearly every recovery is followed by a non-zero EP1 ISO submit that consumes recovery mode.
- The final `usb_ll_iso_after_recovery_dbg = 1` means the last recovery set the flag and no later non-zero submit consumed it.
- Same-parity after recovery is therefore not enough; it keeps the stream alive longer but still produces a high miss/recovery rate.

Change:

- Default `usb_ll_iso_recovery_mode_dbg` changed from `1` to `2`.
- Next experiment is recovery-only FIFO prewrite:
  - normal stream start still uses the old order;
  - only the first non-zero EP1 ISO submit after `IREC` writes FIFO before `EPENA`;
  - odd/even scheduling returns to the normal opposite-parity path.

Next run:

- If `usb_ll_iso_write_pre_enable_used_dbg` grows with `usb_ll_iso_recovery_mode_consumed_dbg`, the new mode is being exercised.
- A useful result would be lower `usbx_iisoixfr_recovery_calls_dbg` per second and more complete JPEG+EOF frames.

## 2026-04-30 23:18:00: recovery-only FIFO prewrite breaks stream start

Fresh USBPcap after `usb_ll_iso_recovery_mode_dbg = 2`:

- File `C:\Users\Professional\Documents\UVC.pcapng`, size `2538836` bytes.
- UVC stream is USB address `38`, EP `0x81`.
- `SET_INTERFACE` for the camera is at `1.576094 s`.
- EP81 after stream start:
  - `96` non-zero IN transfers;
  - `1671` total data bytes;
  - maximum data length `42` bytes;
  - `0` transfers larger than `100` bytes;
  - `50` tiny fragments end in `FF D9`.

Firmware counters from the same run:

- `usb_ll_iso_recovery_mode_dbg = 2`.
- `usb_ll_iso_recovery_mode_used_dbg = 2`.
- `usb_ll_iso_recovery_mode_consumed_dbg = 0x2404`.
- `usb_ll_iso_write_pre_enable_used_dbg = 0x2404`.
- `usbx_iisoixfr_recovery_calls_dbg = 0x2405`.
- `usbx_video_resync_count_dbg = 0x2405`.

Interpretation:

- Mode 2 was definitely exercised on nearly every recovery.
- The firmware `SUBM` trace still shows the intended start payload (`02 00 FF D8 FF E0 ...`).
- The host instead receives only very short JPEG tail fragments, often ending in `FF D9`.
- Writing FIFO before `EPENA` after recovery therefore releases stale/tail data or arms the endpoint in a bad order.

Change:

- Default `usb_ll_iso_recovery_mode_dbg` changed back from `2` to `1`.
- Keep mode 2 available only as a manual GDB experiment; it should not be the normal boot default.

## 2026-05-01: hard stop is an EP81 no-DATAIN lock, not a frame-content problem

Latest USBX run and USBPcap:

- UVC device address in pcap is `14`.
- The camera has only one `SET_INTERFACE` to stream alt `1` at `2.170144 s`; there is no later host alt-0 stop for this device.
- Last non-zero EP81 DATA IN in pcap is at `59.215675 s`.
- Firmware keeps running with `video_last_alt = 1`, `stream_task_state = 0x21`, and continues `VCOM/SUBM/ISOI/DONE/VPAC`.
- `usbx_ep81_last_datain_tick_dbg = 0x12beb`, while submit/done/isoinc advanced to `0x1467b`, so the endpoint spent about `0x1a90` ms (~6.8 s) with no real DATA IN.
- `usbx_dcd_iso_incomplete_calls_dbg == usbx_iisoixfr_recovery_calls_dbg == 0x31f`: every incomplete ISO IN is being turned into a zero-length completion, but the hardware endpoint never resumes real data.

Interpretation:

- The final stream death is the same failure mode seen before: EP81 falls into a persistent submit -> ISO incomplete -> fake DONE loop.
- This is independent of whether the frames are good or broken; payload generation still runs, USBX tasks still run, and the host did not request stream stop.

Change:

- Added an EP81 hard recovery path driven only by the `E8ID` watchdog condition.
- On persistent no-DATAIN while submit/ISOI continue, firmware now logs `FRZE`, schedules `HREC`, aborts/flushes/closes/reopens only endpoint `0x81`, clears the HAL/DCD transfer state, and lets the video task submit again.
- New debug variables: `usbx_ep81_hard_recovery_*`.
