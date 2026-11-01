# Contributing to Perwix / Вклад в развитие Perwix 🚀

<!-- Language Switcher -->
<p align="center">
  <a href="#english">🇬🇧 English</a> •
  <a href="#russian">🇷🇺 Русский</a>
</p>

---

<a id="english"></a>
## English

Thanks for helping with Perwix. It is an early hobby OS, so please keep pull requests focused and describe what is implemented and how you checked it.

### Where to edit

- x86-64 shell commands and their user-visible text: `kernel/core/shell.c`.
- x86-64 boot messages: `kernel/core/main.c`.
- ARM64 serial shell and its text: `kernel/arch/aarch64/main.c`.
- Device status must match the actual implementation in `kernel/drivers/`.
- Project status, build steps and limitations: `README.md`.

Use plain English and ASCII in kernel console strings. The VGA console currently uses the classic text mode character set, so arbitrary Unicode may display incorrectly. Keep help output aligned and remove a command from help if it is not implemented. Do not describe a device as supported just because PCI found it; distinguish detection, initialization, and working transfers.

### Build and run

The Windows build uses Ubuntu in WSL and QEMU from `E:\Program Files\qemu`.

```powershell
wsl -d Ubuntu -- bash -lc 'cd /mnt/e/HybridOS/Perwix-1.0 && ./build.sh'
.\run-qemu.ps1
```

ARM64 preview:

```powershell
wsl -d Ubuntu -- bash -lc 'cd /mnt/e/HybridOS/Perwix-1.0 && ./build-arm64.sh'
.\run-qemu-arm64.ps1
```

In QEMU, type `help` and check the command or driver status you changed. Do not commit generated `build/` files or ISO images.

### Pull request workflow

1. Fork the repository on GitHub.
2. Create a branch for one change, for example `fix/usb-status-text`.
3. Make the change and build the relevant target.
4. Commit with a short message, such as `shell: clarify network status`.
5. Push the branch to your fork and open a pull request against `main`.
6. Describe the change, target (x86-64 or ARM64), build command, and QEMU or hardware result. Mention any limitation that remains.

For a small typo or documentation fix, a concise pull request is enough.

---

<a id="russian"></a>
## Русский

Спасибо за помощь в работе над Perwix. Поскольку это любительский проект операционной системы на ранней стадии разработки, пожалуйста, делайте пул-реквесты (pull requests) узконаправленными: четко описывайте, что именно было реализовано и как вы это проверили.

### Где вносить изменения

- Команды оболочки (shell) для x86-64 и выводимый ими текст: `kernel/core/shell.c`.
- Сообщения при загрузке (x86-64): `kernel/core/main.c`.
- Оболочка через последовательный порт (serial shell) для ARM64 и соответствующий текст: `kernel/arch/aarch64/main.c`.
- Статус устройств должен соответствовать их фактической реализации в `kernel/drivers/`.
- Статус проекта, этапы сборки и ограничения: `README.md`.

Используйте только английский язык и символы ASCII в строках, выводимых в консоль ядра. Консоль VGA в настоящее время использует классический текстовый набор символов, поэтому произвольные символы Unicode могут отображаться некорректно. Следите за тем, чтобы вывод справки (help) был выровнен, и удаляйте из нее команды, которые еще не реализованы. Не указывайте устройство как «поддерживаемое» только на основании того, что оно было обнаружено через шину PCI; различайте этапы обнаружения, инициализации и полноценной передачи данных.

### Сборка и запуск

Для сборки в Windows используется Ubuntu (через WSL) и QEMU (из директории `E:\Program Files\qemu`).

```powershell
wsl -d Ubuntu -- bash -lc 'cd /mnt/e/HybridOS/Perwix-1.0 && ./build.sh'
.\run-qemu.ps1
```

Для ARM64:

```powershell
wsl -d Ubuntu -- bash -lc 'cd /mnt/e/HybridOS/Perwix-1.0 && ./build-arm64.sh'
.\run-qemu-arm64.ps1
```

В QEMU введите команду `help` и проверьте статус измененной вами команды или драйвера. Не добавляйте в репозиторий (не делайте commit) файлы из папки `build/` или образы ISO.

### Порядок работы с пул-реквестами

1. Создайте форк (fork) репозитория на GitHub.
2. Создайте отдельную ветку для каждого изменения, например: `fix/usb-status-text`.
3. Внесите изменения и выполните сборку соответствующего целевого объекта (target).
4. Зафиксируйте изменения (сделайте коммит), добавив краткое описание, например: `shell: clarify network status`.
5. Отправьте (push) ветку в свой форк репозитория и создайте pull request в ветку `main`.
6. Опишите внесенные изменения, целевую архитектуру (x86-64 или ARM64), команду сборки, а также результаты тестирования в QEMU или на реальном оборудовании. Укажите, остались ли какие-либо ограничения.

Для исправления опечаток или внесения правок в документацию достаточно лаконичного pull request’а.
