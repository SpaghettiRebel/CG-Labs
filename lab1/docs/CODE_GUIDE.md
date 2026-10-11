# Гайд по коду ЛР №1

Как последовательно разобраться в коде перед сдачей. Ответы на типовые вопросы защиты — в [LAB1.md](LAB1.md),
требования — в [SPEC.md](SPEC.md). Номера строк актуальны на момент написания.

## 0. Карта проекта

Код делится на четыре слоя. Разбираться нужно в первых двух; шаблон достаточно понимать на уровне «что делает».

| Слой | Файлы | Что внутри |
|---|---|---|
| **Чистая логика** (без Vulkan, покрыта юнит-тестами) | `geometry.*`, `scene.*`, `uniforms.hpp` | цилиндр, камера и проекции, трансформации, анимация, цвета, объекты |
| **Код лабы на Vulkan** | `application.cpp`, `graphics.*`, `shaders/object.*` | буферы, дескрипторы, конвейер, запись команд, интерфейс |
| Шаблон преподавателя | `main.cpp`, `graphics_internal.*` | instance, device, swapchain, render pass, синхронизация, ImGui |
| Тестовая обвязка | `options.*`, `tests/` | переменные `LAB1_*`, юнит- и интеграционные тесты |

Векторы и матрицы — из библиотеки **GLM** (`glm::vec3`, `glm::mat4`, `glm::translate`, …).

## 1. Как устроен кадр — начните отсюда

Цикл в [main.cpp:75](../source/main.cpp#L75) вызывает четыре функции `application` (об этом писал преподаватель):

```
initialize()                    один раз: создать ресурсы Vulkan
loop:
  update(time)                  интерфейс ImGui, изменение состояния сцены (только CPU)
  prepare()        [шаблон]     ждёт fence прошлого кадра, берёт картинку из swapchain
  render(fd)                    записывает команды отрисовки в fd.command_buffer
  submitAndPresent() [шаблон]   дорисовывает ImGui, отправляет команды в очередь, показывает кадр
shutdown()                      один раз: уничтожить всё созданное
```

Ключевой момент: `update` вызывается **до** `prepare()`, т.е. пока GPU может ещё рисовать прошлый кадр.
Поэтому uniform-буферы записываются в `render`, а не в `update` (см. `uploadUniforms`).

## 2. Маршрут чтения

Каждый шаг: что читать → что понять → вопросы для самопроверки.

### Шаг 1. Матрицы через GLM — [scene.cpp](../source/scene.cpp) · лекция 1
- `glm::mat4` хранится **по столбцам** и индексируется `m[столбец][строка]` — как `mat4` в GLSL,
  поэтому копируется в uniform-буфер `memcpy` без транспонирования.
- `glm::translate/rotate/scale(m, …)` умножают **справа**: `m = m · T`. Поэтому цепочка вызовов
  читается как формула слева направо, а к вершине первым применяется последний вызов.
- `modelMatrix` (строка 21): `T · Ry(spin) · Rz · Ry · Rx · S` — масштаб, поворот, перенос.
- `viewMatrix` (128): мир поворачивается вокруг начала координат и отодвигается от камеры на `distance`.
- `projectionMatrix` (113): `glm::perspectiveLH_ZO` и `glm::orthoLH_ZO` — **ровно матрицы со слайдов** лекции 1
  (камера смотрит вдоль +z, глубина [0, 1]); это доказывают юнит-тесты, где матрица со слайда записана поэлементно.
  Обычные `glm::perspective`/`glm::ortho` — вариант OpenGL (камера смотрит вдоль −z, глубина [−1, 1]):
  с ними цилиндр оказывается за спиной камеры, и экран пустой.
- `orthoLH_ZO` принимает диапазон y как (min, max). При y вниз min y — это верх экрана.

Вопросы: почему перенос нельзя записать матрицей 3×3 (однородные координаты, w = 1 у точки и w = 0 у вектора)?
Почему после перспективной матрицы `w = z`, и что делает деление на `w`? Почему нужен именно `LH_ZO`?

### Шаг 2. Цилиндр — [geometry.cpp](../source/geometry.cpp) · вариант 8
- Раскладка вершин (строки 23–40): 2 центра + 50 на верхнем ободе + 50 на нижнем = 102.
- Треугольники (строки 46–60): крышки — веер из центра, бок — 2 треугольника на сегмент, всего 200.
- Порядок обхода: видимая снаружи грань идёт по часовой на экране (`VK_FRONT_FACE_CLOCKWISE`).
- `colorFromPosition` (строка 10): цвет = нормированная позиция в bounding box, одна строка на GLM.

Вопросы: зачем индексный буфер, если можно продублировать вершины? Что будет, если обойти крышку в обратную сторону?

### Шаг 3. Шейдеры и uniform-данные — [object.vert](../shaders/object.vert), [object.frag](../shaders/object.frag), [uniforms.hpp](../source/uniforms.hpp) · лекции 2–3
- `gl_Position = view_projection * model * vec4(in_position, 1)` — вся цепочка преобразований.
- `mix(vec3(1), in_color, parameters.x) * color.rgb` — доп. задания 4 и 5: цвет из UI умножается на цвет вершины.
- `uniforms.hpp` — C++-копии блоков `GlobalUniforms` (set 0) и `ObjectUniforms` (set 1) с раскладкой std140.

Вопросы: чем отличаются `in`/`out`/`uniform`? Почему `vec4 color`, а не `vec3` (выравнивание std140)?

### Шаг 4. Помощники Vulkan — [graphics.cpp](../source/graphics.cpp) · лекции 2–3
- `createBuffer` (12): `VkBuffer` + память через VMA, постоянно отображённая в адресное пространство CPU.
- `writeBuffer` (48): `memcpy` + `vmaFlushAllocation`.
- `loadShaderModule` (54): чтение `.spv` → `VkShaderModule`.
- `createGraphicsPipeline` (86): все стадии конвейера по лекции 2 — input assembly, viewport, rasterizer
  (cull back, clockwise), multisample, depth test LESS, color blend, dynamic state.

Вопросы: зачем viewport и scissor сделаны динамическими? Что делает `cullMode = BACK`?

### Шаг 5. Создание и уничтожение ресурсов — [application.cpp](../source/application.cpp) · лекция 3
Порядок в `initialize` (420): `createGeometry` → `createDescriptors` → `createPipeline`.
- `createGeometry` (97): цилиндр → вершинный и индексный буферы.
- `createDescriptors` (114): uniform-буферы (1 общий + 8 на объекты), два `VkDescriptorSetLayout`,
  пул на 9 наборов, выделение наборов, `vkUpdateDescriptorSets` связывает каждый набор со своим буфером.
- `createPipeline` (212): `VkPipelineLayout` из двух раскладок (set 0, set 1), описание вершинных атрибутов, конвейер.
- `shutdown` (431): уничтожение в обратном порядке. Если что-то забыть — валидация выдаст ошибку при `vkDestroyDevice`.

Вопросы: чем отличаются `VkDescriptorSetLayout`, `VkDescriptorPool`, `VkDescriptorSet`? Почему пул на 9 наборов?

### Шаг 6. Запись команд — `render` (487) · лекции 2–3
Порядок команд, как на последних слайдах лекций:
`uploadUniforms` (492) → `vkBeginCommandBuffer` → `vkCmdBeginRenderPass` (518, очистка цвета и глубины) →
`vkCmdSetViewport/Scissor` (531) → `vkCmdBindPipeline` (534) → `vkCmdBindVertexBuffers` / `vkCmdBindIndexBuffer` (537) →
`vkCmdBindDescriptorSets` для set 0 **один раз** (541) → для каждого объекта: set 1 (544) и `vkCmdDrawIndexed` (546) →
`vkCmdEndRenderPass` → `vkEndCommandBuffer`.

Вопрос: почему в цикле перепривязывается только set 1? (Это суть доп. задания 6.)

### Шаг 7. Логика сцены — [scene.cpp](../source/scene.cpp) · доп. задания
| Функция | Строка | Доп. задание |
|---|---|---|
| `projectionMatrix`, `viewMatrix` | 113, 128 | 1: переключение проекций; камера |
| `modelMatrix` = T · Rz · Ry · Rx · S | 21 | 2: позиция, поворот, масштаб |
| `trajectoryOffset`, `animatedModelMatrix`, `AnimationClock` | 30, 41, 47 | 3: траектория, пауза, скорость |
| `objectUniforms` (`glm::convertSRGBToLinear`) | 95 | 4–5: цвет из UI, умножение на цвет вершин |
| `makeObject`, `freeSlot` | 57, 84 | 6: несколько объектов |
| `orbitCamera`, `zoomCamera` | 103, 108 | управление камерой мышью (сверх задания) |

### Шаг 8. Интерфейс — `update` (450) и функции `*Interface` (308–417)
Интерфейс только меняет переменные состояния (строки 47–58); в `render` они превращаются в матрицы и цвета.
Время анимации (`clock.advance`, 453) идёт до выхода по `hide_ui`, чтобы анимация работала и в тестах.

### Шаг 9 (по желанию). Шаблон — [graphics_internal.cpp](../source/graphics_internal.cpp)
Построчно разбирать не нужно. Достаточно знать:
- `initialize` (414): instance с валидацией, выбор GPU, device, swapchain, depth-буфер, `context.render_pass`
  (цвет + глубина), framebuffer'ы, семафоры и fence;
- `prepare` (770): `vkWaitForFences` → `vkAcquireNextImageKHR`;
- `submitAndPresent` (803): командный буфер ImGui → `vkQueueSubmit` → `vkQueuePresentKHR`, пересоздание swapchain при ресайзе.

## 3. Путь одной вершины — удобно рассказывать на защите

| Этап | Где | Пространство |
|---|---|---|
| Вершина цилиндра `(x, y, z)` + цвет | `makeCylinder` → вершинный буфер | локальное (объекта) |
| `model` = T · Rz · Ry · Rx · S | `modelMatrix`, set 1 | мировое |
| `view` = T(0, 0, D) · Rx(pitch) · Ry(yaw) | `viewMatrix`, set 0 | камеры (камера в начале координат, смотрит вдоль +z) |
| `projection` (орто или перспектива) | `projectionMatrix`, set 0 | clip, однородные координаты |
| деление на `w` (аппаратно) | GPU | NDC: x, y ∈ [−1, 1], z ∈ [0, 1] |
| viewport | `vkCmdSetViewport` | пиксели окна |
| отсечение задних граней, тест глубины | состояние конвейера | — |
| цвет фрагмента → кодирование в sRGB | `object.frag`, swapchain `B8G8R8A8_SRGB` | цвет на экране |

## 4. Есть ли избыточный код

Проверено поиском использования каждой функции в коде приложения и в тестах.

**Лишнего по смыслу нет**: каждый модуль закрывает требование или доп. задание. Своя математическая
библиотека заменена на GLM, найденные мелкие дубли удалены (см. PLAN.md, этап 10).

### Нужно для корректности, хотя задание этого прямо не требует
- Запись uniform-буферов в `render`, а не в `update` — иначе гонка с GPU.
- Перевод цвета sRGB → linear — иначе выбранный цвет на экране светлее.
- Ограничение масштаба снизу (`min_scale`) — отрицательный масштаб выворачивает цилиндр наизнанку.
- Слоты объектов (`freeSlot`) — без них «Add» после «Remove» создавал двойника поверх существующего объекта.
- Управление камерой мышью (~30 строк) — сверх задания, но усиливает «взаимодействие с пользователем».

### Существует только ради тестов
- `options.*` (~140 строк) и `applyStartupOptions` в `application.cpp` (~35 строк): разбор `LAB1_*`,
  чтобы автотесты получали воспроизводимую сцену. Для сдачи не требуется; если спросят — это хуки автотестов.

## 5. Эксперименты «сломай и посмотри»

Лучший способ понять, зачем нужна строка, — изменить её и запустить. Эффект каждой мутации уже
проверен тестами (см. PLAN.md). После эксперимента верните код обратно (`git diff`, затем `git checkout -- файл`).

| Изменение | Что увидите |
|---|---|
| `cylinder_segments = 6` в `application.cpp` | шестигранная призма: видно, из чего состоит «круг» |
| `glm::perspective` вместо `glm::perspectiveLH_ZO` в `scene.cpp` | пустой экран: камера OpenGL смотрит в другую сторону |
| `VK_FRONT_FACE_COUNTER_CLOCKWISE` в `graphics.cpp` | видна внутренность цилиндра вместо внешней стороны |
| `depthTestEnable = VK_FALSE` в `graphics.cpp` | дальние грани рисуются поверх ближних |
| в `modelMatrix` перенести `glm::translate` в конец цепочки | поворот вокруг начала координат, а не вокруг объекта |
| в `objectUniforms` убрать `glm::convertSRGBToLinear` | выбранный в ColorEdit цвет на экране светлее |
| в `render` привязывать `object_sets[0]` для всех объектов | все объекты рисуются в одном месте одним цветом |
| убрать `vkDestroyPipeline` из `shutdown` | ошибка валидации `VUID-vkDestroyDevice-device-05137` при закрытии |
