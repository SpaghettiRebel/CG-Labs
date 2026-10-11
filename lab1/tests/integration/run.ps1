# Integration tests: run the application with validation layers (including synchronization),
# capture frames with VK_LAYER_LUNARG_screenshot and check pixels of the captured images.
#
# Usage (from any directory):
#   powershell -NoProfile -File tests\integration\run.ps1                   # all tests
#   powershell -NoProfile -File tests\integration\run.ps1 -Only baseline    # selected tests, comma separated
#
# Results of every run (frames, validation log, stdout/stderr) are in build-debug\integration\<test>\

param(
	[string[]]$Only = @(),
	[string]$Build = "build-debug"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 3

$root = (Resolve-Path "$PSScriptRoot\..\..").Path
$exe = Join-Path $root "$Build\vulkan-starter-app.exe"
$results_root = Join-Path $root "$Build\integration"
$Only = @($Only | ForEach-Object { $_ -split ',' } | Where-Object { $_ })

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Lab1Window {
	[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
	[DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
}
"@

# ---------------------------------------------------------------------------------------------
# Running the application

# Runs the app until all requested frames are captured, then closes its window.
# $Environment adds or overrides variables (LAB1_*), $WhileRunning gets the process object.
function Invoke-App {
	param(
		[Parameter(Mandatory)] [string]$Name,
		[hashtable]$Environment = @{},
		[int[]]$Frames = @(30),
		[int]$TimeoutSeconds = 30,
		[scriptblock]$WhileRunning = $null
	)

	$dir = Join-Path $results_root $Name
	if (Test-Path $dir) {
		Remove-Item -Recurse -Force $dir
	}
	New-Item -ItemType Directory -Force $dir | Out-Null

	$variables = @{
		VK_KHRONOS_VALIDATION_LOG_FILENAME  = "$dir\validation.txt"
		VK_KHRONOS_VALIDATION_VALIDATE_SYNC = "true"
		VK_INSTANCE_LAYERS                  = "VK_LAYER_LUNARG_screenshot"
		VK_SCREENSHOT_FRAMES                = ($Frames -join ",")
		VK_SCREENSHOT_DIR                   = $dir
		LAB1_HIDE_UI                        = "1"
		LAB1_ANIMATE                        = "0"
	}
	foreach ($key in $Environment.Keys) {
		$variables[$key] = $Environment[$key]
	}

	# The child process copies the environment at start, so restore it right after
	$saved = @{}
	foreach ($key in $variables.Keys) {
		$saved[$key] = [Environment]::GetEnvironmentVariable($key)
		[Environment]::SetEnvironmentVariable($key, $variables[$key])
	}
	try {
		$process = Start-Process -FilePath $exe -WorkingDirectory $root -PassThru `
			-RedirectStandardOutput "$dir\stdout.txt" -RedirectStandardError "$dir\stderr.txt"
		$null = $process.Handle # Keeps the handle, otherwise ExitCode is lost
	} finally {
		foreach ($key in $saved.Keys) {
			[Environment]::SetEnvironmentVariable($key, $saved[$key])
		}
	}

	$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
	while ($process.MainWindowHandle -eq [IntPtr]::Zero -and -not $process.HasExited -and (Get-Date) -lt $deadline) {
		Start-Sleep -Milliseconds 100
		$process.Refresh()
	}

	if ($WhileRunning -and -not $process.HasExited) {
		& $WhileRunning $process
	}

	$frame_files = @($Frames | ForEach-Object { Join-Path $dir "$_.ppm" })
	while (-not $process.HasExited -and (Get-Date) -lt $deadline -and
	       @($frame_files | Where-Object { -not (Test-Path $_) }).Count -gt 0) {
		Start-Sleep -Milliseconds 100
	}
	Start-Sleep -Milliseconds 300 # Let the layer finish writing the last file

	$crashed = $process.HasExited
	$hung = $false
	if (-not $crashed) {
		$process.CloseMainWindow() | Out-Null
		if (-not $process.WaitForExit(10000)) {
			$hung = $true
			$process.Kill()
		}
	}
	$process.WaitForExit()

	$images = @{}
	foreach ($frame in $Frames) {
		$path = Join-Path $dir "$frame.ppm"
		if (Test-Path $path) {
			$images[$frame] = Read-Ppm $path
		}
	}

	[pscustomobject]@{
		Name       = $Name
		Dir        = $dir
		ExitCode   = $process.ExitCode
		Crashed    = $crashed
		Hung       = $hung
		Validation = @(Read-Lines "$dir\validation.txt" | Where-Object { $_ -match '^Validation' })
		Stderr     = @(Read-Lines "$dir\stderr.txt")
		Frames     = $images
	}
}

function Read-Lines([string]$Path) {
	if (Test-Path $Path) {
		@(Get-Content $Path | Where-Object { $_.Trim() -ne "" })
	} else {
		@()
	}
}

# ---------------------------------------------------------------------------------------------
# Images

# Binary PPM (P6) written by the screenshot layer, pixels are RGB bytes
function Read-Ppm([string]$Path) {
	$bytes = [IO.File]::ReadAllBytes($Path)
	$position = 0
	$tokens = New-Object System.Collections.Generic.List[string]
	while ($tokens.Count -lt 4) {
		while ([char]::IsWhiteSpace([char]$bytes[$position])) { $position++ }
		if ([char]$bytes[$position] -eq '#') {
			while ($bytes[$position] -ne 10) { $position++ }
			continue
		}
		$start = $position
		while (-not [char]::IsWhiteSpace([char]$bytes[$position])) { $position++ }
		$tokens.Add([Text.Encoding]::ASCII.GetString($bytes, $start, $position - $start))
	}
	if ($tokens[0] -ne "P6") {
		throw "$Path is not a binary PPM"
	}

	[pscustomobject]@{
		Width  = [int]$tokens[1]
		Height = [int]$tokens[2]
		Bytes  = $bytes
		Offset = $position + 1
	}
}

function Get-Pixel($Image, [int]$X, [int]$Y) {
	$i = $Image.Offset + ($Y * $Image.Width + $X) * 3
	, @([int]$Image.Bytes[$i], [int]$Image.Bytes[$i + 1], [int]$Image.Bytes[$i + 2])
}

function Test-ColorNear($Actual, $Expected, [int]$Tolerance) {
	for ($c = 0; $c -lt 3; $c++) {
		if ([Math]::Abs($Actual[$c] - $Expected[$c]) -gt $Tolerance) {
			return $false
		}
	}
	$true
}

# Background: clear color 0.1 (linear) stored in an sRGB swapchain image = 89
$background = @(89, 89, 89)

# Pixels of the row that differ from the background: count and first/last x
function Measure-Row($Image, [int]$Y) {
	$count = 0
	$first = -1
	$last = -1
	for ($x = 0; $x -lt $Image.Width; $x++) {
		if (-not (Test-ColorNear (Get-Pixel $Image $x $Y) $background 10)) {
			$count++
			if ($first -lt 0) { $first = $x }
			$last = $x
		}
	}
	[pscustomobject]@{ Count = $count; First = $first; Last = $last }
}

# Scene defaults of the application, see scene.hpp and application.cpp
$cylinder_radius = 0.5
$camera_distance = 3.0
$vertical_fov = 60.0
$ortho_height = 3.0

# Straight side view of a white cylinder: no pitch, no vertex colors
$side_view = @{ LAB1_CAMERA_PITCH = "0"; LAB1_COLOR = "1,1,1"; LAB1_VERTEX_COLORS = "0" }

# Number of separate runs of non-background pixels in the row
function Get-RowSegments($Image, [int]$Y) {
	$segments = 0
	$inside = $false
	for ($x = 0; $x -lt $Image.Width; $x++) {
		$is_object = -not (Test-ColorNear (Get-Pixel $Image $x $Y) $background 10)
		if ($is_object -and -not $inside) {
			$segments++
		}
		$inside = $is_object
	}
	$segments
}

# First row from the top that differs from the background in the column, -1 if none
function Get-TopEdge($Image, [int]$X) {
	for ($y = 0; $y -lt $Image.Height; $y++) {
		if (-not (Test-ColorNear (Get-Pixel $Image $X $y) $background 10)) {
			return $y
		}
	}
	-1
}

function Get-Aspect($Image) {
	[double]$Image.Width / $Image.Height
}

# Reflection of a compiled shader: uniform block -> member offsets
function Get-UniformBlocks([string]$Spirv) {
	$json = & spirv-cross $Spirv --reflect | Out-String | ConvertFrom-Json
	$blocks = @{}
	foreach ($ubo in $json.ubos) {
		$type = $json.types.($ubo.type)
		$members = [ordered]@{}
		foreach ($member in $type.members) {
			$members[$member.name] = $member.offset
		}
		$blocks[$type.name] = [pscustomobject]@{
			Set = $ubo.set; Binding = $ubo.binding; Size = $ubo.block_size; Members = $members
		}
	}
	$blocks
}

# ---------------------------------------------------------------------------------------------
# Assertions (throw on failure)

function Assert-True([bool]$Condition, [string]$Message) {
	if (-not $Condition) {
		throw $Message
	}
}

function Assert-Color($Actual, $Expected, [int]$Tolerance, [string]$What) {
	Assert-True (Test-ColorNear $Actual $Expected $Tolerance) `
		"$What is ($($Actual -join ', ')), expected ($($Expected -join ', ')) +-$Tolerance"
}

# No crash or hang, exit code 0, no validation messages (they include leaked objects on shutdown),
# nothing in stderr except allowed lines, all frames captured
function Assert-CleanRun($Run, [string[]]$AllowedStderr = @()) {
	Assert-True (-not $Run.Crashed) "application exited before it was closed (exit code $($Run.ExitCode))"
	Assert-True (-not $Run.Hung) "application did not exit after its window was closed"
	Assert-True ($Run.ExitCode -eq 0) "exit code is $($Run.ExitCode)"

	$validation = $Run.Validation
	Assert-True ($validation.Count -eq 0) `
		"$($validation.Count) validation messages, first: $($validation | Select-Object -First 1)"

	$stderr = @($Run.Stderr | Where-Object { $line = $_; -not ($AllowedStderr | Where-Object { $line -match $_ }) })
	Assert-True ($stderr.Count -eq 0) "stderr: $($stderr | Select-Object -First 3)"

	Assert-True ($Run.Frames.Count -gt 0) "no frames were captured"
}

function Assert-CornersAreBackground($Image) {
	$w = $Image.Width - 1
	$h = $Image.Height - 1
	foreach ($point in @(@(0, 0), @($w, 0), @(0, $h), @($w, $h))) {
		Assert-Color (Get-Pixel $Image $point[0] $point[1]) $background 1 "corner ($($point -join ', '))"
	}
}

# ---------------------------------------------------------------------------------------------
# Tests

$tests = [ordered]@{
	# Application starts, renders and exits cleanly, UI is hidden in tests
	baseline = {
		$run = Invoke-App -Name baseline
		Assert-CleanRun $run
		$image = $run.Frames[30]
		Assert-True ($image.Width -eq 1280 -and $image.Height -eq 720) "frame is $($image.Width)x$($image.Height)"
		Assert-CornersAreBackground $image
	}

	# Uniform blocks of the compiled shader match uniforms.hpp (checked there with static_assert)
	uniform_layout = {
		$blocks = Get-UniformBlocks (Join-Path $root "shaders\object.vert.spv")

		$global = $blocks["GlobalUniforms"]
		Assert-True ($null -ne $global) "GlobalUniforms block not found"
		Assert-True ($global.Set -eq 0 -and $global.Binding -eq 0) "GlobalUniforms is at set $($global.Set), binding $($global.Binding)"
		Assert-True ($global.Size -eq 64) "GlobalUniforms size is $($global.Size), C++ size is 64"
		Assert-True ($global.Members["view_projection"] -eq 0) "view_projection offset is $($global.Members['view_projection'])"

		$object = $blocks["ObjectUniforms"]
		Assert-True ($null -ne $object) "ObjectUniforms block not found"
		Assert-True ($object.Set -eq 1 -and $object.Binding -eq 0) "ObjectUniforms is at set $($object.Set), binding $($object.Binding)"
		Assert-True ($object.Size -eq 96) "ObjectUniforms size is $($object.Size), C++ size is 96"
		$expected = [ordered]@{ model = 0; color = 64; parameters = 80 }
		foreach ($name in $expected.Keys) {
			Assert-True ($object.Members[$name] -eq $expected[$name]) `
				"ObjectUniforms.$name offset is $($object.Members[$name]), C++ offset is $($expected[$name])"
		}
	}

	# Silhouette width in the middle row equals the analytic value for perspective projection:
	# tangents from the camera to the circle of radius R at distance D have tan(a) = R / sqrt(D^2 - R^2)
	perspective_width = {
		$run = Invoke-App -Name perspective_width -Environment ($side_view + @{ LAB1_PROJECTION = "perspective" })
		Assert-CleanRun $run
		$image = $run.Frames[30]

		$tan_half_angle = $cylinder_radius / [Math]::Sqrt($camera_distance * $camera_distance - $cylinder_radius * $cylinder_radius)
		$tan_half_fov = [Math]::Tan($vertical_fov / 2 * [Math]::PI / 180)
		$expected = $image.Width * $tan_half_angle / ((Get-Aspect $image) * $tan_half_fov)

		$row = Measure-Row $image ([int]($image.Height / 2))
		Assert-True ([Math]::Abs($row.Count - $expected) -le 2) ("width is {0} px, expected {1:N1} px" -f $row.Count, $expected)
		Assert-True ([Math]::Abs(($row.First + $row.Last) / 2 - $image.Width / 2) -le 1.5) "cylinder is not centered: x from $($row.First) to $($row.Last)"
		Assert-Color (Get-Pixel $image ($image.Width / 2) ($image.Height / 2)) @(255, 255, 255) 1 "center of white cylinder"
		Assert-CornersAreBackground $image
	}

	# Orthographic: width in pixels = W * 2R / (ortho_height * aspect) = H * 2R / ortho_height
	orthographic_width = {
		$run = Invoke-App -Name orthographic_width -Environment ($side_view + @{ LAB1_PROJECTION = "orthographic" })
		Assert-CleanRun $run
		$image = $run.Frames[30]

		$expected = $image.Height * 2 * $cylinder_radius / $ortho_height
		$row = Measure-Row $image ([int]($image.Height / 2))
		Assert-True ([Math]::Abs($row.Count - $expected) -le 2) ("width is {0} px, expected {1:N1} px" -f $row.Count, $expected)
		Assert-CornersAreBackground $image
	}

	# Side view: the top rim is a straight line in orthographic projection (parallel lines stay parallel),
	# but an arc in perspective (the front of the rim is closer, so it is drawn higher)
	projection_shape = {
		foreach ($type in @("orthographic", "perspective")) {
			$run = Invoke-App -Name "projection_shape_$type" -Environment ($side_view + @{ LAB1_PROJECTION = $type })
			Assert-CleanRun $run
			$image = $run.Frames[30]

			$row = Measure-Row $image ([int]($image.Height / 2))
			$center = [int](($row.First + $row.Last) / 2)
			$side = [int]($center + 0.8 * ($row.Last - $center))
			$center_top = Get-TopEdge $image $center
			$side_top = Get-TopEdge $image $side

			if ($type -eq "orthographic") {
				Assert-True ([Math]::Abs($center_top - $side_top) -le 1) "orthographic top edge is not straight: y = $center_top at center, $side_top at side"
			} else {
				Assert-True ($side_top - $center_top -ge 4) "perspective top edge is not an arc: y = $center_top at center, $side_top at side"
			}
		}
	}

	# After resize the projection uses the new aspect ratio: orthographic width = H * 2R / ortho_height.
	# With a stale aspect the width would follow the image width instead
	resize_aspect = {
		$run = Invoke-App -Name resize_aspect -Frames @(150) -Environment ($side_view + @{ LAB1_PROJECTION = "orthographic" }) -WhileRunning {
			param($process)
			[Lab1Window]::SetWindowPos($process.MainWindowHandle, [IntPtr]::Zero, 50, 50, 1000, 800, 4) | Out-Null
		}
		Assert-CleanRun $run -AllowedStderr @('Swapchain is suboptimal')
		$image = $run.Frames[150]
		Assert-True ($image.Width -lt 1100 -and $image.Height -gt 720) "window was not resized: frame is $($image.Width)x$($image.Height)"

		$expected = $image.Height * 2 * $cylinder_radius / $ortho_height
		$row = Measure-Row $image ([int]($image.Height / 2))
		Assert-True ([Math]::Abs($row.Count - $expected) -le 2) ("width is {0} px, expected {1:N1} px for {2}x{3}" -f $row.Count, $expected, $image.Width, $image.Height)
	}

	# Default camera looks from above: the top cap must be visible, not culled. Its center vertex
	# has procedural color (0.5, 0, 0.5); with wrong winding the inner side of the bottom cap (g = 1) shows up
	top_cap_from_above = {
		$run = Invoke-App -Name top_cap_from_above -Environment @{ LAB1_CAMERA_PITCH = "25"; LAB1_COLOR = "1,1,1"; LAB1_VERTEX_COLORS = "1" }
		Assert-CleanRun $run
		$image = $run.Frames[30]

		# View: rotate by pitch around x, move by camera distance; then perspective divide
		$pitch = 25 * [Math]::PI / 180
		$y = -0.5 * [Math]::Cos($pitch)
		$z = $camera_distance - 0.5 * [Math]::Sin($pitch)
		$ndc_y = $y / ($z * [Math]::Tan($vertical_fov / 2 * [Math]::PI / 180))
		$pixel_y = [int][Math]::Floor(($ndc_y + 1) / 2 * $image.Height)

		# Linear 0.5 encoded by the sRGB swapchain = 188
		Assert-Color (Get-Pixel $image ($image.Width / 2) $pixel_y) @(188, 0, 188) 3 "top cap center at y = $pixel_y"
	}

	# Flat color on screen equals the picked sRGB color: it is converted to linear on the CPU
	# and encoded back by the sRGB swapchain. Without conversion 0.5 would become 188
	flat_color = {
		$run = Invoke-App -Name flat_color -Environment @{ LAB1_CAMERA_PITCH = "0"; LAB1_COLOR = "0.5,0.25,1"; LAB1_VERTEX_COLORS = "0" }
		Assert-CleanRun $run
		$image = $run.Frames[30]
		$expected = @(128, 64, 255) # 0.5 * 255 = 127.5, 0.25 * 255 = 63.75

		Assert-Color (Get-Pixel $image ($image.Width / 2) ($image.Height / 2)) $expected 2 "center of the cylinder"
		# Flat color: the same everywhere on the object
		$row = Measure-Row $image ([int]($image.Height / 2))
		foreach ($x in @(($row.First + 3), ($row.Last - 3))) {
			Assert-Color (Get-Pixel $image $x ($image.Height / 2)) $expected 2 "cylinder side at x = $x"
		}
	}

	# Vertex colors are multiplied by the picked color: top cap center vertex (0.5, 0, 0.5) * linear(0.5, 1, 1)
	vertex_colors = {
		$run = Invoke-App -Name vertex_colors -Environment @{ LAB1_CAMERA_PITCH = "25"; LAB1_COLOR = "0.5,1,1"; LAB1_VERTEX_COLORS = "1" }
		Assert-CleanRun $run
		$image = $run.Frames[30]

		$pitch = 25 * [Math]::PI / 180
		$y = -0.5 * [Math]::Cos($pitch)
		$z = $camera_distance - 0.5 * [Math]::Sin($pitch)
		$pixel_y = [int][Math]::Floor(($y / ($z * [Math]::Tan($vertical_fov / 2 * [Math]::PI / 180)) + 1) / 2 * $image.Height)
		Assert-Color (Get-Pixel $image ($image.Width / 2) $pixel_y) @(92, 0, 188) 3 "top cap center at y = $pixel_y"

		# Gradient: the side of the cylinder isn't a single color
		$row = Measure-Row $image ([int]($image.Height * 0.55))
		$left = Get-Pixel $image ($row.First + 5) ([int]($image.Height * 0.55))
		$right = Get-Pixel $image ($row.Last - 5) ([int]($image.Height * 0.55))
		Assert-True (-not (Test-ColorNear $left $right 20)) "side is not a gradient: ($($left -join ', ')) and ($($right -join ', '))"
	}

	# Three objects, each drawn with its own descriptor set (set = 1): own model matrix and color.
	# Slots from scene::makeObject: x = 0, +1.5, -1.5; palette (sRGB) white, orange, sky blue
	multiple_objects = {
		$run = Invoke-App -Name multiple_objects -Environment @{ LAB1_OBJECTS = "3"; LAB1_CAMERA_PITCH = "0"; LAB1_VERTEX_COLORS = "0" }
		Assert-CleanRun $run
		$image = $run.Frames[30]
		$y = [int]($image.Height / 2)

		$segments = Get-RowSegments $image $y
		Assert-True ($segments -eq 3) "middle row has $segments separate objects, expected 3"

		$tan_half_fov = [Math]::Tan($vertical_fov / 2 * [Math]::PI / 180)
		$objects = @(
			@{ X = 0.0;  Color = @(255, 255, 255) },
			@{ X = 1.5;  Color = @(255, 140, 89) },
			@{ X = -1.5; Color = @(102, 204, 255) }
		)
		foreach ($object in $objects) {
			$ndc_x = $object.X / ($camera_distance * (Get-Aspect $image) * $tan_half_fov)
			$pixel_x = [int](($ndc_x + 1) / 2 * $image.Width)
			Assert-Color (Get-Pixel $image $pixel_x $y) $object.Color 2 "object at x = $($object.X) (pixel $pixel_x)"
		}

		# Background between the objects
		$gap_x = [int]((0.75 / ($camera_distance * (Get-Aspect $image) * $tan_half_fov) + 1) / 2 * $image.Width)
		Assert-Color (Get-Pixel $image $gap_x $y) $background 1 "gap between objects at pixel $gap_x"
	}

	# Paused scene gives identical frames, animation moves the object between frames
	animation = {
		$still = Invoke-App -Name animation_off -Frames @(30, 90) -Environment @{ LAB1_ANIMATE = "0" }
		Assert-CleanRun $still
		$still_hashes = @(30, 90 | ForEach-Object { (Get-FileHash (Join-Path $still.Dir "$_.ppm")).Hash })
		Assert-True ($still_hashes[0] -eq $still_hashes[1]) "frames 30 and 90 differ without animation"

		$moving = Invoke-App -Name animation_on -Frames @(30, 90) -Environment @{ LAB1_ANIMATE = "1" }
		Assert-CleanRun $moving
		$moving_hashes = @(30, 90 | ForEach-Object { (Get-FileHash (Join-Path $moving.Dir "$_.ppm")).Hash })
		Assert-True ($moving_hashes[0] -ne $moving_hashes[1]) "frames 30 and 90 are the same with animation"

		# Trajectory starts at (R + r, 0, 0) from the object position, so the object isn't centered anymore
		$row = Measure-Row $moving.Frames[30] ([int]($moving.Frames[30].Height / 2))
		Assert-True ($row.First -gt $moving.Frames[30].Width / 2) "animated object is not on the right: x from $($row.First) to $($row.Last)"
	}

	# Invalid LAB1_* value is reported instead of silently ignored
	invalid_option = {
		$run = Invoke-App -Name invalid_option -Environment @{ LAB1_OBJECTS = "many" }
		Assert-True (@($run.Stderr | Where-Object { $_ -match 'LAB1_OBJECTS="many" is invalid' }).Count -eq 1) `
			"stderr has no error about LAB1_OBJECTS: $($run.Stderr)"
	}

	# Resize, minimize, restore and maximize without crashes or validation errors
	window_stress = {
		$run = Invoke-App -Name window_stress -Frames @(30) -WhileRunning {
			param($process)
			$window = $process.MainWindowHandle
			foreach ($size in @(@(800, 600), @(1600, 900), @(640, 480), @(1300, 760))) {
				[Lab1Window]::SetWindowPos($window, [IntPtr]::Zero, 50, 50, $size[0], $size[1], 4) | Out-Null
				Start-Sleep -Milliseconds 500
			}
			[Lab1Window]::ShowWindow($window, 6) | Out-Null # Minimize
			Start-Sleep -Seconds 2
			[Lab1Window]::ShowWindow($window, 9) | Out-Null # Restore
			Start-Sleep -Seconds 1
			[Lab1Window]::ShowWindow($window, 3) | Out-Null # Maximize
			Start-Sleep -Seconds 1
			[Lab1Window]::ShowWindow($window, 9) | Out-Null
			Start-Sleep -Seconds 1
		}
		Assert-CleanRun $run -AllowedStderr @('Swapchain is suboptimal')
	}
}

# ---------------------------------------------------------------------------------------------

Assert-True (Test-Path $exe) "$exe not found, build the project first"
foreach ($name in $Only) {
	Assert-True ($tests.Contains($name)) "unknown test '$name', available: $($tests.Keys -join ', ')"
}

$failed = New-Object System.Collections.Generic.List[string]
foreach ($name in $tests.Keys) {
	if ($Only.Count -gt 0 -and $Only -notcontains $name) {
		continue
	}
	$timer = [Diagnostics.Stopwatch]::StartNew()
	try {
		& $tests[$name]
		Write-Host ("PASS  {0} ({1:N1} s)" -f $name, $timer.Elapsed.TotalSeconds) -ForegroundColor Green
	} catch {
		$failed.Add($name)
		Write-Host ("FAIL  {0}: {1}" -f $name, $_.Exception.Message) -ForegroundColor Red
	}
}

if ($failed.Count -gt 0) {
	Write-Host "$($failed.Count) failed: $($failed -join ', ')" -ForegroundColor Red
	exit 1
}
Write-Host "All integration tests passed" -ForegroundColor Green
exit 0
