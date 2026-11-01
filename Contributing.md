# Contributing to Perwix / Вклад в развитие Perwix 🚀

<!-- Language Switcher -->
<p align="center">
  <a href="#english">🇬🇧 English</a> •
  <a href="#russian">🇷🇺 Русский</a>
</p>

---

<a id="english"></a>
## 🇬🇧 English

Thank you for your interest in contributing to Perwix. Perwix is an experimental operating-system kernel. Contributions are welcome, especially reproducible bug reports, documentation improvements, architecture work, and hardware-driver bring-up.

The project is not yet a general-purpose desktop operating system. Please keep implementation status and test claims precise: a controller being detected is not the same as a complete device driver, and a successful compile is not the same as a successful boot on physical hardware.

### 📋 Before you start
- Read `README.md` for the current feature list, limitations, and build instructions.
- Search existing issues and pull requests so work is not duplicated.
- For substantial changes, open an issue first and describe the problem, intended design, target architecture, and test hardware.
- Do not include serial numbers, firmware passwords, personal files, Wi-Fi credentials, or other private information in issues, screenshots, or logs.

### 📁 Project layout
- `kernel/boot/` — 💻 x86-64 Multiboot2 entry code.
- `kernel/arch/x86_64/` — 💻 x86-64 interrupt and MMIO support.
- `kernel/arch/aarch64/` — 📱 experimental ARM64 QEMU startup and kernel.
- `kernel/core/` — console, shell, commands, scheduler, IPC, memory allocator, and Perwix logo.
- `kernel/drivers/` — PCI, USB-controller bring-up, input, and NIC drivers.
- `kernel/net/` — network interfaces and the polling network stack.
- `kernel/fs/` — VFS interfaces and current implementation.
- `grub/` — GRUB configuration and Perwix boot-menu artwork.
- `tools/` — development utilities such as font generation.
- `build.sh` — 💻 x86-64 ELF and hybrid BIOS/UEFI ISO build.
- `build-arm64.sh` — 📱 experimental ARM64 ELF build.
- `run-qemu.ps1` — 💻 x86-64 BIOS-style QEMU test runner.
- `run-qemu-uefi.ps1` — 💻 x86-64 OVMF/UEFI QEMU test runner.
- `run-qemu-arm64.ps1` — 📱 ARM64 QEMU `virt` test runner.

### 💻 Development environments
The documented Windows workflow uses Windows PowerShell 5.1 or later and Ubuntu under WSL. Keep the project in a path without non-ASCII characters when using shell scripts through WSL, for example `E:\Perwix\Perwix-1.2`. This also avoids path-encoding problems in tools that are not fully Unicode-aware.

### 💻 x86-64 build dependencies
Install the required packages in Ubuntu WSL:
```bash
sudo apt update
sudo apt install build-essential nasm grub-common grub-pc-bin \
  grub-efi-amd64-bin xorriso mtools
```

Build the hybrid BIOS/UEFI ISO from PowerShell, adjusting the path if the project is stored elsewhere:
```powershell
wsl -d Ubuntu -- bash -lc 'cd /mnt/e/Perwix/Perwix-1.2 && chmod +x build.sh && ./build.sh'
```
The expected output is: `build/Perwix-1.2-amd64.iso`

`build.sh` checks for GCC, the linker, NASM, `grub-mkrescue`, xorriso, and the GRUB `x86_64-efi` modules. It compiles freestanding x86-64 code with warnings and warnings-as-errors enabled. Do not weaken those checks merely to make a broken change compile.

### 📱 ARM64 preview build
The ARM64 build is a separate experimental QEMU preview; it is not the x86-64 ISO and is not currently a physical ARM device port. Install Clang and LLD in the build environment, then run:
```bash
./build-arm64.sh
```
The expected output is `build/arm64/perwix-arm64.elf`.

### 🧪 Testing changes
Run the tests appropriate to the change and record the exact commands and results in the pull request.

#### 💻 x86-64 QEMU
From PowerShell, after building the ISO:
```powershell
.\run-qemu.ps1
```
The script expects QEMU at `E:\Program Files\qemu\qemu-system-x86_64.exe`. If QEMU is installed elsewhere, edit the `$qemu` path in the script before running it. The default profile emulates a Q35 machine, xHCI, an e1000 NIC, and user-mode networking.

For a UEFI/OVMF test:
```powershell
.\run-qemu-uefi.ps1
```
The script searches common locations beneath the QEMU installation for OVMF firmware. If it cannot find the firmware files, pass their paths explicitly:
```powershell
.\run-qemu-uefi.ps1 -OvmfCode 'C:\path\OVMF_CODE.fd' `
  -OvmfVars 'C:\path\OVMF_VARS.fd'
```
The variables file is copied to `build` before QEMU starts, so the original firmware template is not modified.

#### 🔌 Physical UEFI hardware
Physical-hardware testing is optional. Use a spare USB drive and boot the ISO without installing Perwix, repartitioning a disk, or modifying the Windows boot configuration. Back up important data first. Do not test destructive disk or firmware operations on a machine containing valuable data.

For the first hardware attempt, select **Safe mode + verbose diagnostics** in the GRUB menu. Record the hardware manufacturer and exact model, CPU architecture, firmware mode, and the last visible boot message. A photo of the screen is useful. Snapdragon/ARM models cannot boot the x86-64 image.

A display that works does not imply that the built-in keyboard, touchpad, Wi-Fi, storage, audio, or power management works. Current laptop input support is limited; PS/2 keyboard input is present, while USB HID and I2C-HID are not yet implemented.

### ⚙️ Kernel and driver coding guidelines
- Keep kernel code freestanding. Do not depend on the host C library, dynamic runtime initialization, or hosted operating-system APIs.
- Preserve the build's `-Wall -Wextra -Werror` policy. Fix warnings instead of suppressing them globally.
- Use fixed-width integer types from the project headers for hardware layouts and register values. Validate lengths, offsets, alignment, and address arithmetic before dereferencing bootloader or device data.
- Keep hardware access behind small, documented functions. Use the existing MMIO helpers for memory-mapped registers; do not assume that a physical address is a directly usable C pointer without checking the current mapping.
- Treat device-provided data as untrusted. Check bounds and status bits, handle timeouts, and leave the device in a safe state when initialization fails.
- Do not spin forever waiting for a device. Use bounded polling or a documented timeout and report a useful diagnostic on failure.
- Keep interrupt handlers short. Defer non-trivial work to the existing kernel execution path where practical.
- Avoid introducing unexplained magic numbers. Add register names, bit masks, comments, and a datasheet or specification reference where appropriate.
- Keep architecture-specific code under `kernel/arch/<architecture>/` and make architecture limitations explicit.
- Do not claim a driver is complete because a PCI ID or controller class is detected. Document whether the implementation only detects, resets, enumerates, transfers data, or supports a real device class.

### 🔌 USB and other hardware drivers
The current USB code performs basic host-controller detection and bring-up for UHCI, OHCI, EHCI, and xHCI. It does not yet provide complete USB transfer scheduling, device enumeration, USB HID, or USB mass storage. USB4 router and tunneling support is not implemented.

When contributing a driver, include:
- Supported vendor/device IDs or class/subclass/prog-if values.
- The relevant hardware specification or datasheet reference.
- Initialization and reset sequence, including timeout behavior.
- Register and DMA assumptions, alignment requirements, and memory mapping.
- Error handling and a clear report when the device is unsupported.
- A test record naming the exact QEMU device or physical hardware tested.

Do not label a controller-family stub or an untested implementation as a working device driver.

### 🖥️ Console, UTF-8, and boot artwork
- The original Perwix ASCII artwork is defined in `kernel/core/logo.c`. Reuse that source for the `logo` command, `neofetch`, and boot branding; do not replace it with an unrelated logo.
- Keep ordinary shell and boot messages readable and concise. Use UTF-8 for user-facing text where the framebuffer font supports the glyph.
- The framebuffer console currently supports its bundled bitmap font and a documented set of glyphs. Do not assume every Unicode character is present.
- GRUB configuration belongs in `grub/grub.cfg`; graphical menu layout and assets belong under `grub/themes/perwix/`. Keep a usable text-mode fallback.
- The kernel boot logo is intentionally not printed automatically.

---

<a id="russian"></a>
## 🇷🇺 Русский

Спасибо за ваш интерес к участию в развитии Perwix. Perwix — это экспериментальное ядро операционной системы. Приветствуются любые вклады, особенно воспроизводимые отчеты об ошибках, улучшения документации, работа над архитектурой и запуск драйверов оборудования.

Проект пока не является операционной системой общего назначения для настольных ПК. Пожалуйста, точно указывайте статус реализации и результаты тестов: обнаружение контроллера — это не то же самое, что полноценный драйвер устройства, а успешная компиляция — не то же самое, что успешная загрузка на реальном оборудовании.

### 📋 Перед началом работы
- Прочитайте `README.md` для ознакомления с текущим списком возможностей, ограничениями и инструкциями по сборке.
- Проверяйте существующие задачи (issues) и пул-реквесты (pull requests), чтобы не дублировать работу.
- Для внесения крупных изменений сначала создайте задачу (issue) и опишите проблему, планируемую архитектуру, целевую платформу и оборудование для тестирования.
- Не включайте серийные номера, пароли прошивок, личные файлы, данные Wi-Fi или другую конфиденциальную информацию в задачи, скриншоты или логи.

### 📁 Структура проекта
Будьте обачні, використовуючи код.
• kernel/boot/ — 💻 код входа для x86-64 Multiboot2.
• kernel/arch/x86_64/ — 💻 поддержка прерываний и MMIO для x86-64.
• kernel/arch/aarch64/ — 📱 экспериментальный запуск ядра в QEMU для ARM64.
• kernel/core/ — консоль, оболочка (shell), команды, планировщик, IPC, аллокатор памяти и логотип Perwix.
• kernel/drivers/ — драйверы PCI, запуск USB-контроллеров, ввод и сетевые карты (NIC).
• kernel/net/ — сетевые интерфейсы и сетевой стек на основе опроса (polling).
• kernel/fs/ — интерфейсы VFS и текущая реализация файловой системы.
• grub/ — конфигурация GRUB и оформление загрузочного меню Perwix.
• tools/ — утилиты для разработки, такие как генератор шрифтов.
• build.sh — 💻 сборка x86-64 ELF и гибридного BIOS/UEFI ISO-образа.
• build-arm64.sh — 📱 экспериментальная сборка ELF для ARM64.
• run-qemu.ps1 — 💻 запуск тестов в QEMU в режиме BIOS для x86-64.
• run-qemu-uefi.ps1 — 💻 запуск тестов в QEMU с OVMF/UEFI для x86-64.
• run-qemu-arm64.ps1 — 📱 запуск тестов в QEMU для ARM64 на машине virt.

💻 Среда разработки

Описанный рабочий процесс для Windows использует Windows PowerShell 5.1 или новее и Ubuntu внутри WSL. При использовании скриптов оболочки через WSL храните проект по пути, не содержащему символов, отличных от ASCII, например: E:\Perwix\Perwix-1.2. Это также позволит избежать проблем с кодировкой путей в инструментах, которые не полностью поддерживают Unicode.

💻 Зависимости для сборки x86-64

Установите необходимые пакеты в Ubuntu WSL:
bash sudo apt update sudo apt install build-essential nasm grub-common grub-pc-bin \ grub-efi-amd64-bin xorriso mtools 
Соберите гибридный BIOS/UEFI ISO-образ из PowerShell, изменив путь, если проект сохранен в другом месте:
powershell wsl -d Ubuntu -- bash -lc 'cd /mnt/e/Perwix/Perwix-1.2 && chmod +x build.sh && ./build.sh' 
Ожидаемый результат: build/Perwix-1.2-amd64.iso
Скрипт build.sh проверяет наличие GCC, линкера, NASM, grub-mkrescue, xorriso и модулей GRUB x86_64-efi. Он компилирует изолированный (freestanding) код x86-64 с включенными предупреждениями и обработкой предупреждений как ошибок (-Werror). Не отключайте эти проверки только ради того, чтобы некорректный код смог скомпилироваться.

📱 Предварительная сборка для ARM64

Сборка для ARM64 является отдельным экспериментальным превью для QEMU; это не ISO для x86-64 ноутбуков и на данный момент не порт для физических ARM-платформ. Установите Clang и LLD в среде сборки, затем выполните:
bash ./build-arm64.sh 
Ожидаемый результат: build/arm64/perwix-arm64.elf.

🧪 Тестирование изменений

Запускайте тесты, соответствующие вашим изменениям, и фиксируйте точные команды и результаты в пул-реквесте.

💻 x86-64 QEMU

Из PowerShell после сборки ISO:
powershell .\run-qemu.ps1 
Скрипт ожидает наличие QEMU по пути E:\Program Files\qemu\qemu-system-x86_64.exe. Если QEMU установлен в другом месте, отредактируйте путь $qemu в скрипте перед запуском. Профиль по умолчанию эмулирует чипсет Q35, xHCI, сетевую карту e1000 и сеть в режиме пользователя (user-mode).
Для тестирования UEFI/OVMF:
powershell .\run-qemu-uefi.ps1 
Скрипт ищет файлы прошивки OVMF в стандартных папках внутри директории QEMU. Если он не может их найти, укажите пути к ним явно:
powershell .\run-qemu-uefi.ps1 -OvmfCode 'C:\path\OVMF_CODE.fd' ` -OvmfVars 'C:\path\OVMF_VARS.fd' 
Файл переменных копируется в папку build перед запуском QEMU, поэтому исходный шаблон прошивки не изменяется.

🔌 Реальное оборудование с UEFI

Тестирование на реальном оборудовании необязательно. Используйте свободный USB-накопитель и загружайте ISO без установки Perwix, переразметки диска или изменения конфигурации загрузчика Windows. Сначала обязательно сделайте резервную копию важных данных. Не тестируйте опасные операции с диском или прошивкой на компьютере, содержащем ценные данные.
При первой попытке запуска на железе выберите пункт Safe mode + verbose diagnostics в меню GRUB. Запишите производителя и точную модель оборудования, архитектуру процессора, режим прошивки и последнее видимое загрузочное сообщение. Фотография экрана будет полезной. Модели на Snapdragon/ARM не смогут загрузить образ x86-64.
Если работает дисплей, это еще не означает, что работают встроенная клавиатура, тачпад, Wi-Fi, накопитель, звук или управление питанием. Текущая поддержка ввода на ноутбуках ограничена: работает ввод с клавиатуры PS/2, тогда как поддержка USB HID и I2C-HID еще не реализована.

⚙️ Правила написания кода ядра и драйверов

• Код ядра должен быть изолированным (freestanding). Не зависьте от стандартной библиотеки C хоста, динамической инициализации рантайма или API готовых операционных систем.
• Соблюдайте политику сборки -Wall -Wextra -Werror. Исправляйте предупреждения вместо того, чтобы отключать их глобально.
• Используйте целочисленные типы фиксированной ширины из заголовочных файлов проекта для описания структур оборудования и значений регистров. Проверяйте длину, смещения, выравнивание и арифметику адресов перед разыменованием данных загрузчика или устройств.
• Скрывайте доступ к оборудованию за небольшими, документированными функциями. Используйте существующие хелперы MMIO для регистров, отображаемых в память; не думайте, что физический адрес является напрямую используемым указателем C без проверки текущего маппинга.
• Относитесь к данным от устройств как к ненадежным. Проверяйте границы и биты статуса, обрабатывайте таймауты и оставляйте устройство в безопасном состоянии, если инициализация завершилась ошибкой.
• Не зависайте в бесконечных циклах ожидания устройства. Используйте ограниченный опрос (bounded polling) или документированный таймаут и выводите полезную диагностику при ошибке.
• Делайте обработчики прерываний короткими. По возможности переносите нетривиальную работу в основной поток выполнения ядра.
• Избегайте использования необъяснимых "магических чисел". Добавляйте имена регистров, битовые маски, комментарии и ссылки на спецификации или даташиты там, где это необходимо.
• Храните архитектурно-зависимый код в папке kernel/arch/<архитектура>/ и четко обозначайте ограничения архитектуры.
• Не заявляйте, что драйвер готов, только потому, что был обнаружен PCI ID или класс контроллера. Документируйте, что именно делает реализация: только обнаруживает, сбрасывает, перечисляет устройства, передает данные или поддерживает реальный класс устройств.

🔌 USB и другие драйверы оборудования

Текущий код USB выполняет базовое обнаружение и запуск хост-контроллеров для UHCI, OHCI, EHCI и xHCI. Он пока не обеспечивает полноценное планирование передач USB, перечисление устройств, USB HID или USB-накопители. Поддержка маршрутизации и туннелирования USB4 не реализована.
При добавлении драйвера обязательно включайте:
• Поддерживаемые ID вендоров/устройств или значения class/subclass/prog-if.
• Ссылку на соответствующую аппаратную спецификацию или даташит.
• Последовательность инициализации и сброса, включая поведение при таймаутах.
• Допущения по регистрам и DMA, требования к выравниванию и маппингу памяти.
• Обработку ошибок и четкий отчет, если устройство не поддерживается.
• Отчет о тестировании с указанием конкретного устройства QEMU или протестированного реального железа.
Не выдавайте заглушку для семейства контроллеров или непротестированную реализацию за работающий драйвер устройства.

🖥️ Консоль, UTF-8 и загрузочная графика

• Исходный ASCII-арт Perwix определен в kernel/core/logo.c. Используйте этот исходник для команды logo, neofetch и брендинга загрузки; не заменяйте его сторонними логотипами.
• Делайте обычные сообщения оболочки и загрузки читаемыми и лаконичными. Используйте UTF-8 для текста, отображаемого пользователю, если шрифт фреймбуфера поддерживает нужные глифы.
• Консоль фреймбуфера сейчас поддерживает встроенный растровый шрифт и строго задокументированный набор глифов. Не думайте, что здесь доступен абсолютно любой символ Unicode.
• Конфиксистема GRUB находится в grub/grub.cfg; графическое меню и ресурсы — в папке grub/themes/perwix/. Всегда оставляйте рабочий текстовый режим в качестве резервного.
• Логотип загрузки ядра намеренно не выводится автоматически.
