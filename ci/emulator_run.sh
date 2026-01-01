#!/usr/bin/env bash
# ci/emulator_run.sh
# Запуск и логирование установки APK в эмулятор.
# Не используем `set -u` — на hosted runners это иногда вызывает проблемы с /etc/bash.bashrc.
set -e -o pipefail

mkdir -p emulator_logs

# Логируем версии инструментов (полезно для дебага)
command -v adb >/dev/null 2>&1 && adb --version 2>&1 | sed -n '1,20p' > emulator_logs/adb_version.txt || echo "adb_missing" > emulator_logs/adb_version.txt
command -v aapt2 >/dev/null 2>&1 && aapt2 version 2>&1 | sed -n '1,20p' > emulator_logs/aapt2_version.txt || echo "aapt2_missing" > emulator_logs/aapt2_version.txt
command -v aapt >/dev/null 2>&1 && aapt dump badging --version 2>&1 | sed -n '1,20p' > emulator_logs/aapt_version.txt || echo "aapt_missing" > emulator_logs/aapt_version.txt
command -v sdkmanager >/dev/null 2>&1 && sdkmanager --version 2>&1 > emulator_logs/sdkmanager_version.txt || echo "sdkmanager_missing" > emulator_logs/sdkmanager_version.txt

# Список девайсов до установки
adb devices -l > emulator_logs/adb_devices_before.txt || true

# Ждём девайс (эмулятор) — без таймаута, runner контролирует таймаут
adb wait-for-device

# Проверка APK_PATH
if [ -z "${APK_PATH:-}" ]; then
  echo "APK_PATH_NOT_SET" > emulator_logs/install_log.txt
  exit 0
fi

# Попробуем установить (300s timeout)
timeout 300s adb install -r "$APK_PATH" > emulator_logs/install_log.txt 2>&1 || true

# Попробуем определить pkg и main activity
AAPT2="$(command -v aapt2 || true)"
AAPT="$(command -v aapt || true)"
PKG=""
MAINACT=""

if [ -n "$AAPT2" ]; then
  PKG="$($AAPT2 dump badging "$APK_PATH" 2>/dev/null | awk -F"'" '/package: name=/{print $2; exit}' || true)"
  MAINACT="$($AAPT2 dump badging "$APK_PATH" 2>/dev/null | awk -F"'" '/launchable-activity: name=/{print $2; exit}' || true)"
elif [ -n "$AAPT" ]; then
  PKG="$($AAPT dump badging "$APK_PATH" 2>/dev/null | awk -F"'" '/package: name=/{print $2; exit}' || true)"
  MAINACT="$($AAPT dump badging "$APK_PATH" 2>/dev/null | awk -F"'" '/launchable-activity: name=/{print $2; exit}' || true)"
fi

echo "PKG=${PKG}" > emulator_logs/pkg.txt
echo "MAINACT=${MAINACT}" >> emulator_logs/pkg.txt

# Если пакет найден — пробуем запустить
if [ -n "$PKG" ]; then
  if [ -n "$MAINACT" ]; then
    adb shell am start -n "${PKG}/${MAINACT}" > emulator_logs/launch_log.txt 2>&1 || true
  else
    adb shell monkey -p "${PKG}" -c android.intent.category.LAUNCHER 1 > emulator_logs/launch_log.txt 2>&1 || true
  fi

  sleep 5
  adb logcat -d > emulator_logs/logcat.txt || true
else
  echo "PKG_NOT_DETECTED" > emulator_logs/pkg_error.txt
fi

# Доп. общий лог adb после всех действий
adb devices -l > emulator_logs/adb_devices_after.txt || true

exit 0
