# Окружение для лаб по Vulkan (Windows + MSYS2 GCC + VS Code)

## Что используется

| Компонент | Версия / путь |
|---|---|
| Vulkan SDK | 1.4.321.1, `C:\VulkanSDK\1.4.321.1`, `glslc` в PATH |
| Компилятор | MSYS2 UCRT64 GCC 15.2 (`C:\msys64\ucrt64\bin`) |
| Сборка | CMake 4.2 + Ninja 1.13 (из MSYS2) |
| Отладчик | gdb 17.1 (`pacman -S mingw-w64-ucrt-x86_64-gdb`) |
| Редактор | VS Code: clangd (подсказки), CMake Tools, C/C++ (только отладчик) |
| Шаблон | [vladeemerr/vulkan-starter-app](https://github.com/vladeemerr/vulkan-starter-app), git remote `upstream` |

GLFW, vk-bootstrap, VMA и ImGui скачиваются CMake (FetchContent) при первой конфигурации — это ~2 минуты и нужен доступ к GitHub.

## Сборка и запуск

**VS Code** (открыть папку `lab1`):
- `F5` — собрать Debug и запустить под отладчиком (конфигурация «Debug (gdb)»);
- `Ctrl+Shift+B` — только собрать;
- при первом запуске задача сама выполнит конфигурацию CMake.

**Терминал** (PowerShell или Git Bash — работает в обоих):

```
cmake --preset mingw-debug
cmake --build --preset mingw-debug
.\build-debug\vulkan-starter-app.exe
```

Запускать нужно **из корня проекта** — шейдеры грузятся по относительным путям.
Для Release замените `debug` на `release` (папка `build-release`, без консоли).

**Шейдеры**: положить `имя.vert`/`имя.frag` в `shaders/` и раскомментировать/добавить
`compile_shader(имя.vert)` в `CMakeLists.txt`. `.spv` появится рядом при сборке,
ошибки компиляции выводятся как `shaders/имя.frag:4: error: ...`.

## Что должно получиться

Окно 1280×720: серый фон и окно «Dear ImGui Demo». В Debug-сборке открывается консоль —
в ней **не должно быть** сообщений `Validation Error`.

Скриншот с «шлейфом» из множества окон «Hello world!» — это старая версия шаблона
с пустым `render()`: экран не очищался, а валидация ругалась на пустой командный буфер.

Сообщение `Swapchain is suboptimal for rendering!` при изменении размера окна — норма.

## Что исправлено в шаблоне и почему

Всё проверено на AMD Radeon (драйвер 24.9.1) с включённой валидацией, в том числе
синхронизации. Полный diff с оригиналом: `git diff upstream/master`.

### Приложение (`source/`)

| Проблема | Симптом | Исправление |
|---|---|---|
| AMD не поддерживает `VK_FORMAT_D24_UNORM_S8_UINT` | ошибка создания depth-буфера | автовыбор формата (уже в upstream) + запасные `D32_SFLOAT`, `D16_UNORM` |
| Размер swapchain ≠ размеру framebuffer | картинка не того размера | `set_desired_extent(glfwGetFramebufferSize(...))` при создании swapchain |
| У AMD `minImageCount = 1` → 2 картинки swapchain → пул дескрипторов ImGui на 2 сета, а нужно 3 | «зависание» на старте: assert ImGui `ImTextureData that wasn't uploaded` в невидимом MessageBox | `maxSets` пула из констант ImGui; ошибки Vulkan внутри ImGui теперь печатаются |
| Пустой `application::render()` | `VUID-vkQueueSubmit-pCommandBuffers-00070` (unrecorded command buffer), зависание через пару кадров | запись командного буфера: begin/end render pass с очисткой экрана. Сюда же добавлять `vkCmdBind*`/`vkCmdDraw*` |
| Сообщение печаталось при каждом **успешном** present | спам `Failed to present Vulkan swapchain image` | печать только при реальной ошибке |
| Нет внешней subpass-зависимости у основного render pass | `SYNC-HAZARD-WRITE-AFTER-READ` при sync-валидации | добавлена `VK_SUBPASS_EXTERNAL`-зависимость |
| Свёрнутое окно = поверхность 0×0 | краш при сворачивании | кадры пропускаются, пока окно свёрнуто |
| `main` всегда возвращал 0 | — | возвращает `status` |

### Сборка и инструменты

| Проблема | Симптом | Исправление |
|---|---|---|
| Пресет `mingw-debug` (MinGW Makefiles) находит старый `mingw32-make` из Code::Blocks, который ломает кириллицу в пути | `The C compiler is not able to compile a simple test program` | MinGW-пресеты используют Ninja |
| Git Bash ставит `C:\Program Files\Git\mingw64\bin` в начало PATH, и `cc1.exe` грузит оттуда несовместимые DLL | компилятор молча падает с кодом 1 | пресеты `mingw-*` добавляют `C:\msys64\ucrt64\bin` в начало PATH; задачи VS Code запускаются через `powershell.exe` |
| Exe зависит от `libstdc++-6.dll` из PATH | «точка входа не найдена» при запуске не из той консоли | статическая линковка рантайма GCC (`-static`) |
| `-mwindows` скрывал консоль | не видно сообщений валидации | в Debug консоль есть, в Release — нет |
| `glslc` печатает пути в CP1251 | кракозябры в ошибках шейдеров | `glslc` запускается из корня с относительными путями |
| gdb по умолчанию отключает ASLR, а слой валидации 1.4.321 без ASLR падает | `SIGSEGV` в `VkLayer_khronos_validation.dll` под отладчиком | в `launch.json`: `disable-randomization off` (в ручном gdb: `set disable-randomization off`) |
| clangd без `--query-driver` берёт цель MSVC | `'cstdint' file not found` | `.vscode/settings.json`: `--query-driver` и `compile_commands.json` из `build-debug` |
| Нет `.clang-format`, включён formatOnSave | сохранение переформатирует файл в стиль LLVM | `.clang-format` под стиль шаблона (табы, K&R) |

## Полезное

- Проверка окружения: `glslc --version`, `vulkaninfoSDK --summary`.
- Валидация синхронизации (по умолчанию выключена):
  `$env:VK_KHRONOS_VALIDATION_VALIDATE_SYNC = "true"` перед запуском.
- Новая лаба: скопировать папку `lab1` без `build-*`, открыть в VS Code, `F5`.
- Не кладите проект глубоко: пути внутри `build-*/_deps` добавляют ~110 символов,
  и при длине пути к проекту больше ~140 символов упрётесь в лимит Windows 260
  (`Filename longer than 260 characters`). Кириллица и пробелы в пути — не проблема.
