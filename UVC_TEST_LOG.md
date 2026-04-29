# Журнал UVC тестов

Назначение файла - быстро фиксировать, что именно проверялось, какой был визуальный результат, какие counters и какой pcap относятся к тесту.

## Шаблон записи

```md
## YYYY-MM-DD HH:MM

- Commit:
- Branch:
- Что изменили:
- Как запускали:
- Визуальный результат:
- PCAP:
- Ключевые counters:
- Вывод:
- Следующее действие:
```

## 2026-04-29 - первый опубликованный снимок

- Commit: `0c15f3b Initial STM32H743 UVC project snapshot`
- Branch: `main`
- GitHub: `https://github.com/nsspost/uve.git`
- Состояние: проект опубликован как контрольная точка перед дальнейшей диагностикой UVC/JPEG/display pipeline.
- Примечание: pcap-файлы не хранятся в Git; свежий захват обычно лежит в `C:\Users\Professional\Documents\UVC.pcapng`.

## 2026-04-29 - текущий симптом перед созданием журнала

- Commit: `0c15f3b`
- Branch: `main`
- Что изменили: отдельного нового изменения нет, создана точка фиксации проекта.
- Визуальный результат: после возврата display/SDRAM/JPEG stream может стартовать, но затем замирать; раньше на тестовых кадрах stream был стабильным и без артефактов.
- Ключевое наблюдение: проблема сейчас похожа не на первичную USB enumeration issue, а на срыв устойчивости потока при live frame producer.
- Вывод: следующие эксперименты делать малыми коммитами/ветками и обязательно записывать counters + pcap.

## 2026-04-29 20:28 - поток стартует и затем зависает

- Commit: текущая рабочая копия после возврата baseline `uvc_runtime_flush_before_tx_enable = 1` и `uvc_isoin_incomplete_handling_enable = 1`.
- Branch: `main`.
- Визуальный результат: stream стартовал, затем картинка замерла.
- PCAP: `C:\Users\Professional\Documents\UVC.pcapng`, LastWriteTime `2026-04-29 20:28:43`, размер `11007116` байт.
- Ключевые counters после зависания: `streaming_enabled=1`, `current_alt_setting=1`, `ep_busy=0`, `cnt_eof=48`, `cnt_data_in=12849`, `cnt_iso_in_incomplete=32705`, `cnt_transmit_fail=0`, `cnt_header_only=12908`, `cnt_payload=32646`, `cnt_flush_before_tx=45554`, `last_status=0`, `last_len=512`, `last_header=0x80`, `last_offset=0`, `last_frame_size=1422`.
- Что видно в pcap: `SET_INTERFACE alt=1` прошел на `1.261517 s`; до `2.882965 s` EP `0x81` отдавал валидные UVC/MJPEG payload/header packets. Первый фатальный сбой на `2.898963 s`: `USBD_STATUS_ISOCH_REQUEST_FAILED (0xc0000b00)`, внутри URB все `128` ISO packets имеют `USBD_STATUS_XACT_ERROR (0xc0000011)` и длину `0`.
- После сбоя: полезных данных по EP `0x81` больше нет; при закрытии приложения `SET_INTERFACE alt=0` по EP0 завершается `USBD_STATUS_XACT_ERROR`.
- Вывод: это не похоже на ошибку JPEG, FID/EOF или дескрипторов. Firmware продолжает ставить transfers (`last_status=0`, `cnt_transmit_fail=0`), но USB endpoint/core перестает физически отвечать на IN polling.
- Следующее действие: не менять JPEG/descriptors. Проверять низкоуровневый EP1/FIFO path: per-packet `USBD_LL_FlushEP`, состояние `DIEPCTL/DIEPTSIZ/DIEPINT` вокруг первого срыва, odd/even frame resync и возможность заменить flush-before-every-tx на recovery-only flush.

## 2026-04-29 20:42 - повторный срыв, первые ошибки в header-only окне

- Commit: рабочая копия до эксперимента `flush_policy`.
- Branch: `main`.
- Визуальный результат: stream стартовал и снова завис.
- PCAP: `C:\Users\Professional\Documents\UVC.pcapng`, LastWriteTime `2026-04-29 20:42:48`, размер `18701736` байт.
- Ключевые counters после зависания: `streaming_enabled=1`, `current_alt_setting=1`, `ep_busy=1`, `cnt_eof=18`, `cnt_data_in=4848`, `cnt_iso_in_incomplete=87092`, `cnt_underrun=86955`, `cnt_dropped_frames=87051`, `cnt_prime_ok=91959`, `cnt_header_only=4841`, `cnt_payload=87126`, `cnt_flush_before_tx=91813`, `last_len=512`, `last_header=0x81`, `last_offset=510`, `last_frame_size=1412`.
- Что видно в pcap: устройство `0483:5750`, USB address `57`, EP `0x81`. Последний большой полезный payload был на `2.075773 s`. На `2.091769 s` пришел header-only URB с `224` байтами данных (`02 81` repeated), но уже с `16` ISO descriptors `USBD_STATUS_XACT_ERROR`. На `2.107769 s` следующий URB полностью упал: `USBD_STATUS_ISOCH_REQUEST_FAILED`, `128/128` ISO descriptors `XACT_ERROR`, длина `0`.
- Вывод: первый сбой появляется в промежутке после кадра, на серии header-only packets, а не внутри JPEG payload. Это усиливает гипотезу, что проблема связана с режимом постоянного arm EP1 маленькими пакетами и/или flush перед каждым TX.
- Изменение для следующей прошивки: введен `uvc_runtime_flush_policy = 2` (`recovery-only`), `uvc_runtime_flush_before_tx_enable = 0`; flush теперь выполняется при `SET_INTERFACE`, `IsoINIncomplete` и busy-timeout, но не перед каждым обычным payload/header-only TX. Добавлены counters/register snapshots: `cnt_flush_recovery`, `cnt_flush_while_epena`, `last_flush_reason`, `flush_before_*`, `flush_after_*`.

## 2026-04-29 20:54 - per-TX flush выключен, срыв остался в header-only окне

- Commit: рабочая копия с `uvc_runtime_flush_policy = 2`, `uvc_runtime_flush_before_tx_enable = 0`.
- Branch: `main`.
- Визуальный результат: stream снова завис.
- PCAP: `C:\Users\Professional\Documents\UVC.pcapng`, LastWriteTime `2026-04-29 20:54:06`, размер `12867072` байта.
- Ключевые counters после зависания: `cnt_flush_before_tx=0`, `cnt_flush_recovery=36046`, `cnt_flush_while_epena=36043`, `cnt_iso_in_incomplete=36043`, `cnt_eof=30`, `cnt_header_only=8068`, `cnt_payload=35989`, `last_flush_reason=3`, `flush_before_diepctl=0x80448200`, `flush_before_dieptsiz=0x20080000`, `flush_before_diepint=0x00000080`.
- Что видно в pcap: устройство `0483:5750`, USB address `60`, EP `0x81`. До `2.876699 s` идут нормальные payload URB. На `2.892669 s` header-only URB возвращается как `SUCCESS`, но уже с `50` ISO descriptor errors: `156` байт данных `02 81` и затем нулевые `XACT_ERROR`. На `2.908665 s` следующий URB полностью падает: `USBD_STATUS_ISOCH_REQUEST_FAILED`, `128/128` ISO descriptors `XACT_ERROR`, длина `0`.
- Вывод: flush перед каждым обычным TX не является единственной причиной. Первый сбой по-прежнему возникает в header-only окне. Recovery-flush из `IsoINIncomplete` выполняется при активном endpoint (`EPENA=1`) почти каждый раз, что может усугублять клин.
- Изменение для следующей прошивки: добавлен `uvc_runtime_flush_on_iso_enable = 0` по умолчанию. `IsoINIncomplete` больше не вызывает `USBD_LL_FlushEP()` автоматически; flush на ISO можно включить из отладчика, если понадобится сравнение.

## 2026-04-29 20:58 - без ISO flush поток ломается на первом окне

- Commit: рабочая копия с `uvc_runtime_flush_on_iso_enable = 0`.
- Branch: `main`.
- Визуальный результат: stream не стартовал нормально, экран зеленый.
- PCAP: `C:\Users\Professional\Documents\UVC.pcapng`, LastWriteTime `2026-04-29 20:58:15`, размер `13922600` байт.
- Ключевые counters после зависания: `cnt_eof=1`, `cnt_data_in=25`, `cnt_iso_in_incomplete=149990`, `cnt_dropped_frames=149754`, `cnt_header_only=263`, `cnt_payload=149605`, `cnt_flush_before_tx=0`, `cnt_flush_recovery=3`, `cnt_flush_while_epena=0`.
- Что видно в pcap: устройство `0483:5750`, USB address `62`, EP `0x81`. Единственный ненулевой UVC URB - frame `917` на `1.040000 s`: `3106` байт, но `103/128` ISO descriptors с `XACT_ERROR`. Данные начинаются не с UVC header `02 xx`, а с хвоста JPEG (`01 ca 1f 63`, затем `ff d9`), потом идут payload/header-only fragments. Следующий URB уже полностью `128/128 XACT_ERROR`.
- Вывод: отключать flush/resync из `IsoINIncomplete` нельзя - поток теряет синхронизацию сразу и хост получает кусок кадра не с начала.
- Изменение для следующей прошивки: вернуть `uvc_runtime_flush_on_iso_enable = 1`, но включить диагностический `uvc_runtime_no_frame_gap_enable = 1`, чтобы не ждать `uvc_frame_interval_ms` между кадрами и резко уменьшить/убрать серии header-only packets.

## 2026-04-29 21:02 - `no_frame_gap = 1` разогнал поток слишком сильно

- Commit: рабочая копия с `uvc_runtime_no_frame_gap_enable = 1`.
- Branch: `main`.
- Визуальный результат: stream упал почти после первого кадра.
- PCAP: `C:\Users\Professional\Documents\UVC.pcapng`, LastWriteTime `2026-04-29 21:02:44`, размер `18691292` байта.
- Ключевые counters: `cnt_eof=33`, `cnt_data_in=101`, `cnt_iso_in_incomplete=114531`, `cnt_header_only=0`, `cnt_payload=114664`, `cnt_flush_before_tx=0`, `cnt_flush_recovery=114573`, `last_len=512`, `last_header=0x80`, `last_frame_size=1412`.
- Что видно в pcap: для USB address `64` и EP `0x81` есть ровно один ненулевой UVC URB: frame `1703`, time `1.434938 s`, `Packet Data Length=47818`, `Isochronous transfer error count=26`. Внутри повторяется `512,512,394`, что соответствует множеству полных кадров по `1412` байт в одном host URB window.
- Вывод: `no_frame_gap = 1` не решение, а диагностическое подтверждение: мы убрали header-only окно ценой нарушения заявленного frame interval. Нужно не слать кадры без паузы, а согласованно уменьшить `dwFrameInterval`.
- Изменение для следующей прошивки: `uvc_runtime_no_frame_gap_enable = 0`; `UVC_FRAME_INTERVAL_100NS = 100000` (`10 ms`); `UVC_FRAME_RATE` вычисляется из interval; flush policy остается recovery-only.

## 2026-04-29 21:12 - 10 ms pacing все еще падает в header-only окне

- Commit: рабочая копия с `UVC_FRAME_INTERVAL_100NS = 100000`, `uvc_runtime_no_frame_gap_enable = 0`.
- Branch: `main`.
- Визуальный результат: stream стартовал, затем завис.
- PCAP: `C:\Users\Professional\Documents\UVC.pcapng`, LastWriteTime `2026-04-29 21:12:53`, размер `9657732` байта.
- Ключевые counters: `cnt_eof=241`, `cnt_data_in=19247`, `cnt_iso_in_incomplete=45264`, `cnt_header_only=18554`, `cnt_payload=45958`, `cnt_flush_before_tx=0`, `cnt_flush_recovery=45267`, `cnt_flush_while_epena=45264`, `last_len=512`, `last_header=0x81`, `last_offset=510`, `last_frame_size=1422`.
- Что видно в pcap: устройство `0483:5750`, USB address `10`, EP `0x81`. Полезные URB идут примерно раз в `16 ms`, то есть поток больше не разгоняется одним огромным залпом. Последний ненулевой URB: frame `4775`, time `3.677778 s`, `Packet Data Length=998`, `Isochronous transfer error count=80`. Внутри сначала идет хвост кадра (`512`, `394`), затем длинная серия `2`-байтных header-only packets, а потом `XACT_ERROR`.
- Вывод: 10 ms уменьшил окно, но не убрал первопричину. Первый сбой по-прежнему появляется после EOF, во время серии header-only packets.
- Изменение для следующей прошивки: не arm-ить EP1 header-only пакетами в межкадровом ожидании. Если `next_frame_tick` еще в будущем, `UVC_PrimeNextPacket()` теперь просто пропускает постановку transfer. `IsoINIncomplete` в таком idle-gap больше не делает flush/requeue.
- Для отладки добавлена компактная структура `uvc_watch`; ее удобнее смотреть вместо полного `uvc_runtime_dbg`.

## 2026-04-29 21:19 - idle skip убрал header-only, но no-arm тоже роняет поток

- Commit: рабочая копия с `uvc_runtime_idle_header_only_enable = 0`, idle-gap без `USBD_LL_Transmit`.
- Branch: `main`.
- Визуальный результат: stream снова завис.
- PCAP: `C:\Users\Professional\Documents\UVC.pcapng`, LastWriteTime `2026-04-29 21:19:19`, размер `7713784` байта.
- Ключевые counters: `cnt_eof=361`, `cnt_data_in=1083`, `cnt_iso_in_incomplete=21197`, `cnt_header_only=0`, `cnt_idle_gap_skip=27796`, `cnt_idle_iso_skip=0`, `cnt_payload=22281`, `cnt_flush_recovery=21200`, `cnt_flush_while_epena=21197`, `last_len=512`, `last_header=0x80`, `last_offset=510`, `last_frame_size=1422`.
- Что видно в pcap: устройство `0483:5750`, USB address `12`, EP `0x81`. Header-only пакетов больше нет. Полезные URB: `1418/1428` байт = один полный кадр, `2836/2856` байт = два полных кадра в одном 16-ms host URB. Последний ненулевой URB: frame `4817`, time `4.985506 s`, `Packet Data Length=906`, `Isochronous transfer error count=90`. Внутри только `512 + 394` байта payload, затем много нулевых ISO descriptors и `XACT_ERROR`.
- Вывод: просто не arm-ить EP1 в idle-gap нельзя. Для HS isoch IN хост продолжает polling, и после пустого участка следующий payload уже не восстанавливает поток.
- Изменение для следующей прошивки: добавить режим idle ZLP. Между кадрами теперь по умолчанию ставится isoch IN transfer длиной `0`, а не UVC header-only и не skip. Новый режим: `uvc_runtime_idle_packet_mode = UVC_IDLE_PACKET_ZLP`. Новый счетчик: `uvc_watch.cnt_idle_zlp`.

## 2026-04-29 21:25 - idle ZLP не дал принципиального результата

- Commit: рабочая копия с `uvc_runtime_idle_packet_mode = UVC_IDLE_PACKET_ZLP`.
- Branch: `main`.
- Визуальный результат: stream снова не стал устойчивым.
- PCAP: `C:\Users\Professional\Documents\UVC.pcapng`, LastWriteTime `2026-04-29 21:25:28`, размер `6277708` байт.
- Ключевые counters: `cnt_eof=460`, `cnt_data_in=36759`, `cnt_iso_in_incomplete=16893`, `cnt_header_only=0`, `cnt_idle_zlp=35398`, `cnt_payload=18254`, `cnt_flush_recovery=16877`, `cnt_flush_while_epena=16874`, `last_len=512`, `last_header=0x81`.
- Что видно в pcap: устройство `0483:5750`, USB address `11`, EP `0x81`. Header-only нет. ZLP/нулевые ISO descriptors присутствуют. Есть частичный URB frame `2873`, time `2.542069 s`, `Packet Data Length=916`, `Isochronous transfer error count=63`; внутри много нулевых descriptors, чередующихся с `XACT_ERROR`, затем `512 + 404` payload, потом снова ошибки. После этого поток еще продолжался до примерно `5.7 s`, но в итоге снова ушел в нулевые URB.
- Вывод: варианты idle-gap (`header-only`, skip, ZLP) не дают устойчивого решения на текущем classic STM USB Device stack. Это уже больше похоже на проблему низкоуровневого isoch IN scheduling/recovery, чем на JPEG/FID/descriptor.
- Решение по направлению: сохранить текущий результат как checkpoint и начать отдельную ветку/эксперимент с USBX `Ux_Device_Video`, не ломая текущий проект.
