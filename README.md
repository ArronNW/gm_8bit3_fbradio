# gmsv_eightbit3 2.0.2

Серверный бинарный модуль для обработки голосовых пакетов Garry's Mod. Клиентам модуль и файлы пресетов не нужны: сервер отправляет им обычные `svc_VoiceData` пакеты.

## Поддерживаемые серверы

| Сервер | Файл |
|---|---|
| Windows x86 | `gmsv_eightbit_win32.dll` |
| Windows x86-64 | `gmsv_eightbit_win64.dll` |
| Linux x86 | `gmsv_eightbit_linux.dll` |
| Linux x86-64 | `gmsv_eightbit_linux64.dll` |

Нужный файл помещается только на сервер в `garrysmod/lua/bin/`. Lua загружает его через `require("eightbit")`.

## Раздельный голос для каждого слушателя

Модуль маршрутизирует голос после стандартной проверки `PlayerCanHearPlayersVoice`. Для одной пары «слушатель — говорящий» можно отправить изменённый пакет, а всем остальным — исходный пакет.

```lua
require("eightbit")

-- Во время звонка A слышит B и B слышит A через телефон.
eightbit.SetReceiverEffect(a:UserID(), b:UserID(), eightbit.EFF_TELEPHONE)
eightbit.SetReceiverEffect(b:UserID(), a:UserID(), eightbit.EFF_TELEPHONE)

-- Стоящий рядом третий игрок не имеет такого маршрута и слышит обычный голос.

-- Завершение звонка.
eightbit.ClearReceiverEffect(a:UserID(), b:UserID())
eightbit.ClearReceiverEffect(b:UserID(), a:UserID())
```

`EnableEffect(speakerUserId, effectId)` сохранён для совместимости: он задаёт эффект говорящего по умолчанию для всех слушателей. `SetReceiverEffect` имеет приоритет над ним; `EFF_NONE` принудительно оставляет голос чистым для конкретной пары.

При отключении игрока вызывайте `ClearPlayer(userId)`. `ClearAllRoutes()` очищает все парные маршруты. `SetShouldHearEffect(listener, speaker, false)` принудительно оставляет пару без эффекта, а `true` снова наследует эффект говорящего по умолчанию.

## Серверные пресеты

Модуль читает `*.gvox` из `garrysmod/data/gvox/presets/`. Обновлённый gVOX получает файлы через Lua-поиск `DATA` и передаёт их содержимое прямо модулю, поэтому загрузка не зависит от рабочей папки процесса `srcds`. Формат декларативный `key=value`: произвольный C++/C# код из `data` намеренно не компилируется и не исполняется.

```ini
version=1
name=Field Radio
description=Clear military radio with light static
gain=1.0
highpass_hz=720
lowpass_hz=3300
drive=1.45
clip=0.82
pitch=1.0
ring_frequency_hz=0
ring_mix=0
echo_delay_ms=1.4
echo_mix=0.12
noise=0.006
quantize=260
downsample=2
soft_clip=false
```

Поддерживаемые поля: `name`, `description`, `key`, `gain`, `highpass_hz`, `lowpass_hz`, `drive`, `clip`, `pitch`, `ring_frequency_hz`, `ring_mix`, `echo_delay_ms`, `echo_mix`, `noise`, `quantize`, `downsample`, `soft_clip`. Неизвестные поля игнорируются для прямой совместимости формата.

Идентификатор пользовательского пресета стабильно вычисляется из имени файла и лежит в диапазоне 1000–65534. Так как Garry's Mod не разрешает расширение `.gvox` в стандартном `file.Write`, server-side аддоны создают такие файлы через `eightbit.WritePresetFile(fileName, content)`. После создания файлов вызовите Lua-хук `gVOXPresetsChanged`; обновлённый gVOX перечитает каталог автоматически.

## Lua API

- `EnableEffect(speakerUserId, effectId)` — общий эффект говорящего.
- `SetReceiverEffect(listenerUserId, speakerUserId, effectId)` — эффект только для пары.
- `ClearReceiverEffect(listenerUserId, speakerUserId)` — убрать парное переопределение.
- `GetReceiverEffect(listenerUserId, speakerUserId)` — эффективный ID для пары.
- `SetShouldHearEffect(listenerUserId, speakerUserId, bool)` — принудительно чистый голос либо наследование общего эффекта.
- `ClearPlayer(userId)`, `ClearAllRoutes()` — очистка состояния.
- `WritePresetFile(fileName, content)` — безопасно проверить и создать `.gvox` в серверной папке пресетов.
- `ReloadPresets([directory])` — перечитать пресеты.
- `GetPresets()`, `GetPresetName(id)`, `IsValidPreset(id)`, `GetPresetDirectory()` — каталог пресетов.
- `IsHookAvailable()` — найден ли голосовой хук текущего движка.
- `GetVersion()` — версия модуля.
- Старые `EnableBroadcast`, `SetBroadcastIP`, `SetBroadcastPort`, `SetGainFactor`, `SetCrushFactor`, `GetCrushFactor`, `SetDesampleRate` сохранены.

Встроенные эффекты имеют прежние ID 0–14. Новые маршруты используют UserID, не EntityIndex.

## Сборка

Проект переведён на generator v2 `garrysmod_common`, платформы `x86`/`x86_64` и отдельные библиотеки `opus/lib32`/`opus/lib64`. Для Linux x86 `libopus.a` собран с PIC. Linux Release-сборки рассчитаны на glibc 2.17 и новее.
