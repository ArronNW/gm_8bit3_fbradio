# API gmsv_eightbit 2.0.2

## Пример телефона

```lua
require("eightbit")

eightbit.SetReceiverEffect(caller:UserID(), callee:UserID(), eightbit.EFF_TELEPHONE)
eightbit.SetReceiverEffect(callee:UserID(), caller:UserID(), eightbit.EFF_TELEPHONE)

-- У третьего игрока маршрута нет: стоя рядом, он слышит исходный голос.

eightbit.ClearReceiverEffect(caller:UserID(), callee:UserID())
eightbit.ClearReceiverEffect(callee:UserID(), caller:UserID())
```

`SetReceiverEffect(listener, speaker, effect)` имеет приоритет над общим `EnableEffect(speaker, effect)`. Передача `eightbit.EFF_NONE` принудительно оставляет чистый голос для конкретного слушателя. Стандартные ограничения `PlayerCanHearPlayersVoice` и 3D-голос продолжают применяться.

При `PlayerDisconnected` вызывайте `eightbit.ClearPlayer(ply:UserID())`.

## Формат `data/gvox/presets/*.gvox`

```ini
version=1
name=My Telephone
description=Custom server-only telephone voice
highpass_hz=500
lowpass_hz=3200
drive=1.25
clip=0.86
quantize=380
downsample=2
noise=0.004
echo_delay_ms=0.7
echo_mix=0.13
```

Доступные параметры: `name`, `description`, `key`, `gain`, `highpass_hz`, `lowpass_hz`, `drive`, `clip`, `pitch`, `ring_frequency_hz`, `ring_mix`, `echo_delay_ms`, `echo_mix`, `noise`, `quantize`, `downsample`, `soft_clip`.

Стандартный `file.Write` Garry's Mod не разрешает расширение `.gvox`. Поэтому server-side аддон должен записывать пресет через модуль:

```lua
require("eightbit")

local written, result = eightbit.WritePresetFile("my_telephone.gvox", [[
version=1
name=My Telephone
lowpass_hz=3200
]])

if not written then
	ErrorNoHalt("Не удалось создать пресет: " .. tostring(result) .. "\n")
	return
end
```

Имя может содержать латинские буквы, цифры, `.`, `_` и `-` и обязательно должно оканчиваться на `.gvox`. Модуль проверяет формат и записывает файл только в `data/gvox/presets`.

После записи файла вызовите:

```lua
hook.Run("gVOXPresetsChanged")
```

Обновлённый gVOX перечитает каталог, отправит клиенту только названия/ID и обновит список. Сам файл пресета остаётся на сервере. Даже без этого хука gVOX обнаружит изменение каталога не позднее чем через 5 секунд.

## Основные функции

- `EnableEffect(speakerUserId, effectId)`
- `SetReceiverEffect(listenerUserId, speakerUserId, effectId)`
- `ClearReceiverEffect(listenerUserId, speakerUserId)`
- `GetReceiverEffect(listenerUserId, speakerUserId)`
- `SetShouldHearEffect(listenerUserId, speakerUserId, bool)`
- `ClearPlayer(userId)`, `ClearAllRoutes()`
- `WritePresetFile(fileName, content)`, `ReloadPresets([directory])`
- `GetPresets()`, `GetPresetName(id)`, `IsValidPreset(id)`
- `GetPresetDirectory()`, `IsHookAvailable()`, `GetVersion()`
