# Сборка и проверка

Для сборки нужны C++20, CMake, Ninja и Vulkan SDK с компилятором шейдеров glslc. Для запуска нужны драйвер Vulkan 1.3 и поддержка dynamicRendering. Здесь используется GCC из MSYS2 UCRT64. При чистой сборке CMake загружает зависимости через FetchContent.

Из корня проекта:

```powershell
cmake --preset mingw-debug
cmake --build --preset mingw-debug
.\build-debug\vulkan-starter-app.exe
```

Для Release используются пресет `mingw-release` и каталог `build-release`. Рабочий каталог при запуске — корень `lab4`, где находятся `shaders` и `textures`.

## Тесты

```powershell
ctest --test-dir build-debug --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File tests/integration/run.ps1
```

Оконные тесты используют слои валидации и захвата кадров из Vulkan SDK. Скрипт сохраняет кадры и журналы в `build-debug/integration`. Для отдельной проверки теней добавьте `-Only floor_shadows`.
