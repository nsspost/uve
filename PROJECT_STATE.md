# Состояние проекта

Этот файл - короткая внешняя память проекта. Если контекст чата потерян, начинать восстановление отсюда, затем смотреть `UVC_DEBUG_HYPOTHESES.md`, историю Git и свежий `pcap`.

## Платформа

- MCU: STM32H743.
- HSE: 8 MHz.
- IDE/сборка: STM32CubeIDE project, текущая рабочая сборка через `Debug/makefile`.
- USB: USB HS через ULPI PHY.
- UVC: MJPEG stream через HS isochronous IN endpoint.
- Дисплей/SDRAM/JPEG: используются в текущей ветке экспериментов.

## Важные ограничения

- Не включать DCache без отдельного решения и проверки всех DMA/USB/JPEG/LTDC буферов.
- Не включать USB DMA вслепую.
- Не регенерировать проект из `.ioc`: проект уже сильно правился руками.
- Не откатывать "победные" USB-изменения без явного коммита перед экспериментом.
- Не лечить поток случайными костылями, пока не понятна причина очередного срыва.

## Текущее состояние Git

- GitHub: `https://github.com/nsspost/uve.git`
- Основная ветка: `main`
- Первый опубликованный снимок: `0c15f3b Initial STM32H743 UVC project snapshot`

Перед рискованной гипотезой лучше создать ветку:

```powershell
git checkout -b test/<short-hypothesis-name>
```

После удачного состояния сразу фиксировать:

```powershell
git status
git add .
git commit -m "<short stable state description>"
git push
```

## Диагностические файлы

- Основной журнал гипотез: `UVC_DEBUG_HYPOTHESES.md`
- Журнал запусков и pcap: `UVC_TEST_LOG.md`
- Текущий захват USB обычно обновляется здесь: `C:\Users\Professional\Documents\UVC.pcapng`

## Что уже было важным достижением

- Удалось получить стабильный UVC stream на тестовых кадрах без артефактов.
- После этого начали возвращать SDRAM, display, JPEG pipeline и увеличивать размер кадра.
- Текущая проблема сместилась от "битые пакеты/артефакты" к устойчивости потока при live JPEG/display pipeline: stream может стартовать, потом замирать или не всегда стартовать.

## Что смотреть при срыве потока

В debugger-visible counters:

- `streaming_enabled`
- `ep_busy`
- `current_alt_setting`
- `frame_active`
- `fid`
- `cnt_sof`
- `cnt_data_in`
- `cnt_iso_in_incomplete`
- `cnt_underrun`
- `cnt_dropped_frames`
- `cnt_eof`
- `cnt_frame_load`
- `cnt_prime_calls`
- `cnt_prime_ok`
- `cnt_header_only`
- `cnt_payload`
- `last_status`
- `last_len`
- `last_header`
- `last_offset`
- `last_frame_size`
- `last_submit_tick`
- `last_complete_tick`

По camera/JPEG pipeline:

- `camera_pipeline_last_jpeg_size`
- `camera_pipeline_skip_oversize_jpeg`
- `camera_pipeline_encode_ok`
- `camera_pipeline_submit_ok`
- `camera_pipeline_skip_pending`
- `camera_pipeline_skip_jpeg_busy`
- `jpeg_encode_fail`
- `jpeg_fail_timeout`

## Рабочая гипотеза на сейчас

USB transport уже умеет доходить до качественного изображения, но full pipeline может не успевать или неправильно владеть буферами кадра. При следующем анализе не начинать с дескрипторов заново; сначала проверить:

- есть ли готовый JPEG до первого UVC payload;
- не меняется ли/не перезаписывается ли JPEG buffer до завершения передачи кадра;
- соответствует ли реальный темп готовых JPEG заявленному frame interval;
- что происходит в pcap в момент перехода от нормальных payload к `IsoINIncomplete`/header-only/drop;
- не застревает ли UVC state machine в `ep_busy` или в состоянии активного, но недозавершенного кадра.
