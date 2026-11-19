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

Add-Type -AssemblyName System.Drawing
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
$cylinder_segments = 50
$camera_distance = 3.0
$vertical_fov = 60.0
$ortho_height = 3.0

# Lights and material (sRGB colors), angles in degrees
$ambient_light = @{ Color = @(1, 1, 1); Intensity = 0.05 }
$default_lights = @{
	directional = @{ Color = @(1, 1, 1); Intensity = 0.6; Yaw = 30; Pitch = 50 }
	point       = @{ Position = @(0.8, -0.4, -0.9); Color = @(1, 0.6, 0.3); Intensity = 1.0 }
	spot        = @{ Position = @(-0.55, -1, -1.4); Color = @(0.5, 0.8, 1); Intensity = 2.0; Yaw = 30; Pitch = 35; Inner = 8; Outer = 16 }
}
$material_specular = @(1, 1, 1)
$material_shininess = 32

# Materials of scene.hpp by index in LAB1_MATERIAL
$material_indices = @{ bricks = 0; lava = 1; metal = 2; wood = 3 }
$material_textures = @{
	bricks = @{ Albedo = "bricks_albedo"; Specular = "bricks_specular" }
	lava   = @{ Albedo = "lava_albedo"; Specular = "lava_specular"; Emissive = "lava_emissive" }
	metal  = @{ Albedo = "metal_albedo"; Specular = "metal_specular" }
	wood   = @{ Albedo = "wood_albedo" }
}

# Copy of the environment with added or replaced variables
function Join-Environment($Base, $Extra) {
	$result = $Base.Clone()
	foreach ($key in $Extra.Keys) {
		$result[$key] = $Extra[$key]
	}
	$result
}

# Straight side view of one wooden cylinder lit only by the ambient light
$side_view = @{ LAB1_CAMERA_PITCH = "0"; LAB1_OBJECTS = "1"; LAB1_MATERIAL = "3"; LAB1_LIGHTS = "none" }

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

# Reflection of a compiled shader: block name -> set, binding, member offsets.
# For a storage buffer with an array the members are those of the array element, Stride is the array stride
function Get-ShaderBlocks([string]$Spirv) {
	$json = & spirv-cross $Spirv --reflect | Out-String | ConvertFrom-Json
	$blocks = @{}
	foreach ($kind in @("ubos", "ssbos", "push_constants")) {
		if (-not ($json.PSObject.Properties.Name -contains $kind)) {
			continue
		}
		foreach ($resource in $json.$kind) {
			$type = $json.types.($resource.type)
			$members = @($type.members)
			$stride = $null
			if ($members[0].PSObject.Properties.Name -contains "array_stride") {
				$stride = $members[0].array_stride
				$members = @($json.types.($members[0].type).members)
			}
			$offsets = [ordered]@{}
			foreach ($member in $members) {
				$offsets[$member.name] = $member.offset
			}
			$is_push_constant = $kind -eq "push_constants"
			$blocks[$type.name] = [pscustomobject]@{
				Set     = if ($is_push_constant) { $null } else { $resource.set }
				Binding = if ($is_push_constant) { $null } else { $resource.binding }
				Stride  = $stride
				Members = $offsets
			}
		}
	}
	$blocks
}

# ---------------------------------------------------------------------------------------------
# Expected colors: Blinn-Phong as in shaders/object.frag, computed independently of the C++ code

function Vec-Add($a, $b) { , @(($a[0] + $b[0]), ($a[1] + $b[1]), ($a[2] + $b[2])) }
function Vec-Sub($a, $b) { , @(($a[0] - $b[0]), ($a[1] - $b[1]), ($a[2] - $b[2])) }
function Vec-Mul($a, $b) { , @(($a[0] * $b[0]), ($a[1] * $b[1]), ($a[2] * $b[2])) }
function Vec-Scale($a, [double]$k) { , @(($a[0] * $k), ($a[1] * $k), ($a[2] * $k)) }
function Vec-Dot($a, $b) { $a[0] * $b[0] + $a[1] * $b[1] + $a[2] * $b[2] }
function Vec-Normalize($a) { Vec-Scale $a (1 / [Math]::Sqrt((Vec-Dot $a $a))) }

function ConvertTo-Radians([double]$Degrees) { $Degrees * [Math]::PI / 180 }

function ConvertTo-LinearColor($Srgb) {
	, @($Srgb | ForEach-Object { if ($_ -le 0.04045) { $_ / 12.92 } else { [Math]::Pow(($_ + 0.055) / 1.055, 2.4) } })
}

# Linear color -> bytes written by the sRGB swapchain
function ConvertTo-Srgb8($Linear) {
	, @($Linear | ForEach-Object {
		$c = [Math]::Min([Math]::Max($_, 0), 1)
		$srgb = if ($c -le 0.0031308) { 12.92 * $c } else { 1.055 * [Math]::Pow($c, 1 / 2.4) - 0.055 }
		[int][Math]::Round(255 * $srgb)
	})
}

# Pixels of a texture file as [y, x, channel] bytes
$texture_cache = @{}
function Get-Texture([string]$Name) {
	if (-not $texture_cache.ContainsKey($Name)) {
		$bitmap = New-Object System.Drawing.Bitmap (Join-Path $root "textures\$Name.png")
		$texels = New-Object 'byte[,,]' $bitmap.Height, $bitmap.Width, 3
		for ($y = 0; $y -lt $bitmap.Height; $y++) {
			for ($x = 0; $x -lt $bitmap.Width; $x++) {
				$c = $bitmap.GetPixel($x, $y)
				$texels[$y, $x, 0] = $c.R
				$texels[$y, $x, 1] = $c.G
				$texels[$y, $x, 2] = $c.B
			}
		}
		$texture_cache[$Name] = [pscustomobject]@{ Width = $bitmap.Width; Height = $bitmap.Height; Texels = $texels }
		$bitmap.Dispose()
	}
	$texture_cache[$Name]
}

# Like the sampler: bilinear filtering of the 4 nearest texels, coordinates repeat.
# With -Srgb bytes are converted to linear before filtering, as VK_FORMAT_R8G8B8A8_SRGB does
function Get-TextureSample([string]$Name, [double]$U, [double]$V, [switch]$Srgb) {
	$texture = Get-Texture $Name
	$x = $U * $texture.Width - 0.5
	$y = $V * $texture.Height - 0.5
	$x0 = [Math]::Floor($x)
	$y0 = [Math]::Floor($y)
	$fx = $x - $x0
	$fy = $y - $y0
	$result = @(0.0, 0.0, 0.0)
	foreach ($corner in @(@(0, 0, ((1 - $fx) * (1 - $fy))), @(1, 0, ($fx * (1 - $fy))), @(0, 1, ((1 - $fx) * $fy)), @(1, 1, ($fx * $fy)))) {
		$tx = (([int]$x0 + $corner[0]) % $texture.Width + $texture.Width) % $texture.Width
		$ty = (([int]$y0 + $corner[1]) % $texture.Height + $texture.Height) % $texture.Height
		$texel = @(($texture.Texels[$ty, $tx, 0] / 255.0), ($texture.Texels[$ty, $tx, 1] / 255.0), ($texture.Texels[$ty, $tx, 2] / 255.0))
		if ($Srgb) {
			$texel = ConvertTo-LinearColor $texel
		}
		$result = Vec-Add $result (Vec-Scale $texel $corner[2])
	}
	, $result
}

# Forward direction for yaw and pitch, as scene::forwardDirection: yaw turns right, pitch turns down (y points down)
function Get-Direction([double]$Yaw, [double]$Pitch) {
	$yaw_radians = ConvertTo-Radians $Yaw
	$pitch_radians = ConvertTo-Radians $Pitch
	, @(([Math]::Sin($yaw_radians) * [Math]::Cos($pitch_radians)), [Math]::Sin($pitch_radians),
		([Math]::Cos($yaw_radians) * [Math]::Cos($pitch_radians)))
}

# Spot light cone factor at the point: 1 inside the inner angle, 0 outside the outer one, linear in the cosine between
function Get-SpotCone($Light, $Point) {
	$to_point = Vec-Normalize (Vec-Sub $Point $Light.Position)
	$cos_angle = Vec-Dot $to_point (Get-Direction $Light.Yaw $Light.Pitch)
	$cos_inner = [Math]::Cos((ConvertTo-Radians $Light.Inner))
	$cos_outer = [Math]::Cos((ConvertTo-Radians ([Math]::Max($Light.Outer, $Light.Inner))))
	[Math]::Min([Math]::Max(($cos_angle - $cos_outer) / [Math]::Max($cos_inner - $cos_outer, 1e-4), 0), 1)
}

# Animated sampling of object.frag: two copies of the albedo image with moving coordinates and a tint changing in time
function Get-AnimatedSample([string]$Material, [double]$U, [double]$V, [double]$Time) {
	$files = $material_textures[$Material]
	$first = Get-TextureSample $files.Albedo ($U + 0.1 * $Time) $V -Srgb
	$second = Get-TextureSample $files.Albedo (2 * $U) (2 * $V + 0.2 * $Time) -Srgb
	$tint = @((0.6 + 0.4 * [Math]::Cos($Time)), (0.6 + 0.4 * [Math]::Cos($Time + 2.094)), (0.6 + 0.4 * [Math]::Cos($Time + 4.189)))
	$sample = Get-MaterialSample $Material $U $V
	$sample.Albedo = Vec-Scale (Vec-Mul (Vec-Add $first $second) $tint) 0.5
	$sample
}

# Material of the point with texture coordinates (u, v), linear; a missing texture is a black pixel
function Get-MaterialSample([string]$Material, [double]$U, [double]$V) {
	$files = $material_textures[$Material]
	@{
		Albedo   = Get-TextureSample $files.Albedo $U $V -Srgb
		Specular = if ($files.ContainsKey("Specular")) { Get-TextureSample $files.Specular $U $V } else { , @(0, 0, 0) }
		Emissive = if ($files.ContainsKey("Emissive")) { Get-TextureSample $files.Emissive $U $V -Srgb } else { , @(0, 0, 0) }
	}
}

# Color (sRGB bytes) of the point with normal N seen from the camera, material from Get-MaterialSample,
# lit by the ambient light and the named default lights (directional, point, spot)
function Get-ExpectedColor($Point, $N, $Camera, $Material, [string[]]$Lights) {
	$v = Vec-Normalize (Vec-Sub $Camera $Point)
	$specular = Vec-Mul (ConvertTo-LinearColor $material_specular) $Material.Specular
	$color = Vec-Scale (Vec-Mul (ConvertTo-LinearColor $ambient_light.Color) $Material.Albedo) $ambient_light.Intensity

	foreach ($name in $Lights) {
		$light = $default_lights[$name]
		$radiance = Vec-Scale (ConvertTo-LinearColor $light.Color) $light.Intensity
		if ($name -eq "directional") {
			$l = Vec-Scale (Get-Direction $light.Yaw $light.Pitch) -1
		} else {
			$to_light = Vec-Sub $light.Position $Point
			$l = Vec-Normalize $to_light
			$radiance = Vec-Scale $radiance (1 / (Vec-Dot $to_light $to_light)) # Inverse square law
			if ($name -eq "spot") {
				$radiance = Vec-Scale $radiance (Get-SpotCone $light $Point)
			}
		}

		$n_dot_l = Vec-Dot $N $l
		if ($n_dot_l -gt 0) {
			$h = Vec-Normalize (Vec-Add $l $v)
			$highlight = [Math]::Pow([Math]::Max((Vec-Dot $N $h), 0), $material_shininess)
			$reflected = Vec-Add (Vec-Scale $Material.Albedo $n_dot_l) (Vec-Scale $specular $highlight)
			$color = Vec-Add $color (Vec-Mul $radiance $reflected)
		}
	}
	ConvertTo-Srgb8 (Vec-Add $color $Material.Emissive)
}

# Camera at the distance from the origin looking at it with the pitch (LAB1_CAMERA_PITCH), no yaw
function Get-CameraPosition([double]$Pitch) {
	Vec-Scale (Get-Direction 0 $Pitch) (-$camera_distance)
}

# Pixel of a world point for the perspective camera of Get-CameraPosition
function Get-ScreenPoint($Image, $Point, [double]$Pitch) {
	# View = R^T * (p - camera): rotation around x by the pitch
	$q = Vec-Sub $Point (Get-CameraPosition $Pitch)
	$pitch_radians = ConvertTo-Radians $Pitch
	$y = $q[1] * [Math]::Cos($pitch_radians) - $q[2] * [Math]::Sin($pitch_radians)
	$z = $q[1] * [Math]::Sin($pitch_radians) + $q[2] * [Math]::Cos($pitch_radians)
	$tan_half_fov = [Math]::Tan((ConvertTo-Radians ($vertical_fov / 2)))
	$ndc_x = $q[0] / ($z * (Get-Aspect $Image) * $tan_half_fov)
	$ndc_y = $y / ($z * $tan_half_fov)
	, @([int][Math]::Floor(($ndc_x + 1) / 2 * $Image.Width), [int][Math]::Floor(($ndc_y + 1) / 2 * $Image.Height))
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

function Assert-Block($Blocks, [string]$Name, $Set, $Binding, $Members, $Stride = $null) {
	$block = $Blocks[$Name]
	Assert-True ($null -ne $block) "$Name block not found"
	Assert-True ($block.Set -eq $Set -and $block.Binding -eq $Binding) "$Name is at set $($block.Set), binding $($block.Binding)"
	Assert-True ($block.Stride -eq $Stride) "$Name array stride is $($block.Stride), C++ size is $Stride"
	foreach ($member in $Members.Keys) {
		Assert-True ($block.Members[$member] -eq $Members[$member]) `
			"$Name.$member offset is $($block.Members[$member]), C++ offset is $($Members[$member])"
	}
	Assert-True ($block.Members.Count -eq $Members.Count) "$Name has members $($block.Members.Keys -join ', ')"
}

# Side view (LAB1_CAMERA_PITCH = 0): the center column of the image shows the front line of the cylinder
# x = 0, z = -R cos(pi / segments). It is the middle of a flat side face, where the interpolated normal is (0, 0, -1),
# u = 3 * 0.75 and v goes from 0 at the top to 1 at the bottom. Every 2nd row must have the expected color.
# With $AnimatedTime >= 0 the material has the animated sampling at this time. Contrast edges of a texture move by a small error of u or v,
# so a color of the neighbor texels is accepted too. Returns the points with their texture coordinates
function Assert-FrontLine($Image, [string]$Material, [string[]]$Lights, [double]$AnimatedTime = -1) {
	$z = -$cylinder_radius * [Math]::Cos([Math]::PI / $cylinder_segments)
	$tan_half_fov = [Math]::Tan((ConvertTo-Radians ($vertical_fov / 2)))
	$camera = Get-CameraPosition 0
	$x = [int]($Image.Width / 2)
	$depth = $z - $camera[2]

	# The column is half a pixel to the right of x = 0: u grows along the chord of the segment
	$world_x = 1.0 / $Image.Width * (Get-Aspect $Image) * $tan_half_fov * $depth
	$chord = 2 * $cylinder_radius * [Math]::Sin([Math]::PI / $cylinder_segments)
	$u = 2.25 + $world_x / $chord * (3.0 / $cylinder_segments)

	$texel = 1.0 / 512
	$points = New-Object System.Collections.Generic.List[object]
	for ($row = 0; $row -lt $Image.Height; $row += 2) {
		$y = (($row + 0.5) / $Image.Height * 2 - 1) * $tan_half_fov * $depth
		if ([Math]::Abs($y) -gt 0.48) {
			continue # Edges of the side
		}
		$v = $y + 0.5
		$point = @(0, $y, $z)
		$actual = Get-Pixel $Image $x $row

		$expected = $null
		$matched = $false
		foreach ($offset in @(@(0, 0), @(0, -1), @(0, 1), @(-1, 0), @(1, 0))) {
			$sample_u = $u + $offset[0] * $texel
			$sample_v = $v + $offset[1] * $texel
			$sample = if ($AnimatedTime -ge 0) { Get-AnimatedSample $Material $sample_u $sample_v $AnimatedTime } else { Get-MaterialSample $Material $sample_u $sample_v }
			$candidate = Get-ExpectedColor $point @(0, 0, -1) $camera $sample $Lights
			if ($null -eq $expected) {
				$expected = $candidate
			}
			if (Test-ColorNear $actual $candidate 3) {
				$matched = $true
				break
			}
		}
		Assert-True $matched ("front line point y = {0:N3} (row {1}) is ({2}), expected ({3}) +-3" -f $y, $row, ($actual -join ', '), ($expected -join ', '))
		$points.Add(@($point, $u, $v))
	}
	Assert-True ($points.Count -gt 50) "front line has only $($points.Count) checked rows"
	, $points.ToArray()
}

# Mean color of the row segment [First, Last] as bytes
function Get-MeanColor($Image, [int]$Y, [int]$First, [int]$Last) {
	$sum = @(0.0, 0.0, 0.0)
	for ($x = $First; $x -le $Last; $x++) {
		$sum = Vec-Add $sum (Get-Pixel $Image $x $Y)
	}
	, (Vec-Scale $sum (1.0 / ($Last - $First + 1)))
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

	# Blocks of the compiled shaders match uniforms.hpp (checked there with static_assert):
	# set = 0 is the scene with storage buffers of lights, set = 1 is the object
	uniform_layout = {
		$scene = [ordered]@{
			view_projection = 0; camera_position = 64; point_light_count = 76; directional_light_direction = 80
			spot_light_count = 92; directional_light_color = 96; ambient_light = 112
		}
		$object = [ordered]@{ model = 0; specular = 64; shininess = 76 }

		foreach ($shader in @("object.vert", "object.frag")) {
			$blocks = Get-ShaderBlocks (Join-Path $root "shaders\$shader.spv")
			Assert-Block $blocks "SceneUniforms" 0 0 $scene
			Assert-Block $blocks "ObjectUniforms" 1 0 $object
		}

		$blocks = Get-ShaderBlocks (Join-Path $root "shaders\object.frag.spv")
		Assert-Block $blocks "PointLights" 0 1 ([ordered]@{ position = 0; color = 16 }) 32
		Assert-Block $blocks "SpotLights" 0 2 ([ordered]@{ position = 0; cos_inner = 12; direction = 16; cos_outer = 28; color = 32 }) 48

		Assert-Block $blocks "ObjectConstants" $null $null ([ordered]@{ animated_sampling = 0; time = 4 })

		# Combined image samplers of the material set
		$reflection = & spirv-cross (Join-Path $root "shaders\object.frag.spv") --reflect | Out-String | ConvertFrom-Json
		$expected = @{ albedo_texture = 1; specular_texture = 2; emissive_texture = 3 }
		foreach ($sampler in $expected.Keys) {
			$found = @($reflection.textures | Where-Object { $_.name -eq $sampler })
			Assert-True ($found.Count -eq 1 -and $found[0].set -eq 1 -and $found[0].binding -eq $expected[$sampler]) "$sampler is not at set 1, binding $($expected[$sampler])"
		}

		$blocks = Get-ShaderBlocks (Join-Path $root "shaders\light_marker.vert.spv")
		Assert-Block $blocks "MarkerConstants" $null $null ([ordered]@{ model_view_projection = 0; color = 64 })
	}

	# Silhouette width in the middle row equals the analytic value for perspective projection:
	# tangents from the camera to the circle of radius R at distance D have tan(a) = R / sqrt(D^2 - R^2)
	perspective_width = {
		$run = Invoke-App -Name perspective_width -Environment (Join-Environment $side_view @{ LAB1_PROJECTION = "perspective" })
		Assert-CleanRun $run
		$image = $run.Frames[30]

		$tan_half_angle = $cylinder_radius / [Math]::Sqrt($camera_distance * $camera_distance - $cylinder_radius * $cylinder_radius)
		$tan_half_fov = [Math]::Tan($vertical_fov / 2 * [Math]::PI / 180)
		$expected = $image.Width * $tan_half_angle / ((Get-Aspect $image) * $tan_half_fov)

		$row = Measure-Row $image ([int]($image.Height / 2))
		Assert-True ([Math]::Abs($row.Count - $expected) -le 2) ("width is {0} px, expected {1:N1} px" -f $row.Count, $expected)
		Assert-True ([Math]::Abs(($row.First + $row.Last) / 2 - $image.Width / 2) -le 1.5) "cylinder is not centered: x from $($row.First) to $($row.Last)"
		Assert-CornersAreBackground $image
	}

	# Orthographic: width in pixels = W * 2R / (ortho_height * aspect) = H * 2R / ortho_height
	orthographic_width = {
		$run = Invoke-App -Name orthographic_width -Environment (Join-Environment $side_view @{ LAB1_PROJECTION = "orthographic" })
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
			$run = Invoke-App -Name "projection_shape_$type" -Environment (Join-Environment $side_view @{ LAB1_PROJECTION = $type })
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
		$run = Invoke-App -Name resize_aspect -Frames @(150) -Environment (Join-Environment $side_view @{ LAB1_PROJECTION = "orthographic" }) -WhileRunning {
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

	# Without lights the color is ambient * albedo texture: the texels are sampled at the right coordinates,
	# sRGB texels are converted to linear by the texture format. No specular and emissive textures: black pixels
	ambient_only = {
		$run = Invoke-App -Name ambient_only -Environment $side_view
		Assert-CleanRun $run
		$null = Assert-FrontLine $run.Frames[30] "wood" @()
	}

	# Directional light: Blinn-Phong on the front line; the right side faces away from the light
	# (the light goes to +x), so it has only the ambient light: dark at any texel
	directional_light = {
		$run = Invoke-App -Name directional_light -Environment (Join-Environment $side_view @{ LAB1_LIGHTS = "directional" })
		Assert-CleanRun $run
		$image = $run.Frames[30]

		$null = Assert-FrontLine $image "wood" @("directional")
		$y = [int]($image.Height / 2)
		$row = Measure-Row $image $y
		$right = Get-Pixel $image ($row.Last - 3) $y
		Assert-True ($right[0] -le 45 -and $right[1] -le 45 -and $right[2] -le 45) "right side is lit: ($($right -join ', '))"
	}

	# Point light: Blinn-Phong with the inverse square law, the front line points are at different distances
	point_light = {
		$run = Invoke-App -Name point_light -Environment (Join-Environment $side_view @{ LAB1_LIGHTS = "point"; LAB1_SHOW_LIGHTS = "0" })
		Assert-CleanRun $run
		$null = Assert-FrontLine $run.Frames[30] "wood" @("point")
	}

	# Spot light aimed at the front line: full light inside the inner angle, a soft edge between
	# the inner and the outer angles, only the ambient light outside
	spot_light = {
		$run = Invoke-App -Name spot_light -Environment (Join-Environment $side_view @{ LAB1_LIGHTS = "spot"; LAB1_SHOW_LIGHTS = "0" })
		Assert-CleanRun $run
		$points = Assert-FrontLine $run.Frames[30] "wood" @("spot")

		$cones = @($points | ForEach-Object { Get-SpotCone $default_lights.spot $_[0] })
		$full = @($cones | Where-Object { $_ -eq 1 }).Count
		$soft = @($cones | Where-Object { $_ -gt 0.05 -and $_ -lt 0.95 }).Count
		$dark = @($cones | Where-Object { $_ -eq 0 }).Count
		Assert-True ($full -ge 10 -and $soft -ge 10 -and $dark -ge 10) "front line rows: $full in full light, $soft on the soft edge, $dark outside"
	}

	# Light markers: cube of the light color at the point and the spot light, white lines of the spot cone,
	# no lighting. Without "Show lights" there is only the background
	light_markers = {
		$environment = @{ LAB1_CAMERA_PITCH = "25"; LAB1_OBJECTS = "1"; LAB1_MATERIAL = "3"; LAB1_LIGHTS = "point,spot" }
		$shown = Invoke-App -Name light_markers_shown -Environment (Join-Environment $environment @{ LAB1_SHOW_LIGHTS = "1" })
		$hidden = Invoke-App -Name light_markers_hidden -Environment (Join-Environment $environment @{ LAB1_SHOW_LIGHTS = "0" })
		Assert-CleanRun $shown
		Assert-CleanRun $hidden
		$shown_image = $shown.Frames[30]
		$hidden_image = $hidden.Frames[30]

		foreach ($name in @("point", "spot")) {
			$light = $default_lights[$name]
			$pixel = Get-ScreenPoint $shown_image $light.Position 25
			# Constant color on every face: the linear light color is encoded back to the picked sRGB color
			$expected = @($light.Color | ForEach-Object { [int][Math]::Round(255 * $_) })
			foreach ($offset in @(@(0, 0), @(-4, -4), @(4, 4))) {
				$x = $pixel[0] + $offset[0]
				$y = $pixel[1] + $offset[1]
				Assert-Color (Get-Pixel $shown_image $x $y) $expected 1 "$name light marker at ($x, $y)"
			}
			Assert-Color (Get-Pixel $hidden_image $pixel[0] $pixel[1]) $background 1 "hidden $name light marker"
		}

		# White cone lines around the middle of the cone axis
		$spot = $default_lights.spot
		$middle = Vec-Add $spot.Position (Vec-Scale (Get-Direction $spot.Yaw $spot.Pitch) 0.3)
		$center = Get-ScreenPoint $shown_image $middle 25
		$white = @{ shown = 0; hidden = 0 }
		for ($y = $center[1] - 40; $y -le $center[1] + 40; $y++) {
			for ($x = $center[0] - 40; $x -le $center[0] + 40; $x++) {
				if (Test-ColorNear (Get-Pixel $shown_image $x $y) @(255, 255, 255) 0) { $white.shown++ }
				if (Test-ColorNear (Get-Pixel $hidden_image $x $y) @(255, 255, 255) 0) { $white.hidden++ }
			}
		}
		Assert-True ($white.shown -ge 30 -and $white.hidden -eq 0) "white pixels near the spot cone: $($white.shown) shown, $($white.hidden) hidden"
	}

	# The specular texture says what shines. On the bricks the highlight of the spot light is only on wet patches,
	# not in the mortar; the wood has the black pixel instead of the texture and no highlight (spot_light test)
	specular_texture = {
		$run = Invoke-App -Name specular_texture -Environment (Join-Environment $side_view @{ LAB1_MATERIAL = "0"; LAB1_LIGHTS = "spot"; LAB1_SHOW_LIGHTS = "0" })
		Assert-CleanRun $run
		$points = Assert-FrontLine $run.Frames[30] "bricks" @("spot")

		$shining = @($points | Where-Object { (Get-MaterialSample "bricks" $_[1] $_[2]).Specular[0] -gt 0.5 -and (Get-SpotCone $default_lights.spot $_[0]) -eq 1 }).Count
		$dull = @($points | Where-Object { (Get-MaterialSample "bricks" $_[1] $_[2]).Specular[0] -lt 0.1 -and (Get-SpotCone $default_lights.spot $_[0]) -eq 1 }).Count
		Assert-True ($shining -ge 5 -and $dull -ge 5) "front line rows in full light: $shining shining, $dull dull"
	}

	# The emissive texture is added after the lighting: cracks of the lava glow without any light.
	emissive_texture = {
		$run = Invoke-App -Name emissive_texture -Environment (Join-Environment $side_view @{ LAB1_MATERIAL = "1" })
		Assert-CleanRun $run
		$points = Assert-FrontLine $run.Frames[30] "lava" @()

		$glowing = @($points | Where-Object { (Get-MaterialSample "lava" $_[1] $_[2]).Emissive[0] -gt 0.2 }).Count
		Assert-True ($glowing -ge 3) "only $glowing rows of the front line cross a glowing crack"
	}

	# Animated sampling with the time stopped by LAB1_SAMPLING_TIME: two copies of the image and a tint, by the formula
	animated_sampling_formula = {
		$run = Invoke-App -Name animated_sampling_formula -Environment (Join-Environment $side_view @{ LAB1_ANIMATED_SAMPLING = "1"; LAB1_SAMPLING_TIME = "1.5" })
		Assert-CleanRun $run
		$null = Assert-FrontLine $run.Frames[30] "wood" @() 1.5
	}

	# Animated sampling depends on the time, without the flag the image doesn't change
	animated_sampling_time = {
		$still = Invoke-App -Name animated_sampling_off -Frames @(30, 90) -Environment (Join-Environment $side_view @{ LAB1_ANIMATED_SAMPLING = "0" })
		Assert-CleanRun $still
		$hashes = @(30, 90 | ForEach-Object { (Get-FileHash (Join-Path $still.Dir "$_.ppm")).Hash })
		Assert-True ($hashes[0] -eq $hashes[1]) "frames 30 and 90 differ without animated sampling"

		$moving = Invoke-App -Name animated_sampling_on -Frames @(30, 90) -Environment (Join-Environment $side_view @{ LAB1_ANIMATED_SAMPLING = "1" })
		Assert-CleanRun $moving
		$hashes = @(30, 90 | ForEach-Object { (Get-FileHash (Join-Path $moving.Dir "$_.ppm")).Hash })
		Assert-True ($hashes[0] -ne $hashes[1]) "frames 30 and 90 are the same with animated sampling"
	}

	# Top cap from above: not culled, its center has uv = (0.5, 0.5), lit by the directional light
	top_cap = {
		$run = Invoke-App -Name top_cap -Environment @{ LAB1_CAMERA_PITCH = "25"; LAB1_OBJECTS = "1"; LAB1_MATERIAL = "3"; LAB1_LIGHTS = "directional" }
		Assert-CleanRun $run
		$image = $run.Frames[30]

		$top_center = @(0, -0.5, 0)
		$pixel = Get-ScreenPoint $image $top_center 25
		$expected = Get-ExpectedColor $top_center @(0, -1, 0) (Get-CameraPosition 25) (Get-MaterialSample "wood" 0.5 0.5) @("directional")
		Assert-Color (Get-Pixel $image ($image.Width / 2) $pixel[1]) $expected 4 "top cap center at y = $($pixel[1])"
	}

	# Three objects read their own elements of the dynamic uniform buffer: own model matrices
	multiple_objects = {
		$run = Invoke-App -Name multiple_objects -Environment (Join-Environment $side_view @{ LAB1_OBJECTS = "3" })
		Assert-CleanRun $run
		$image = $run.Frames[30]
		$y = [int]($image.Height / 2)

		$segments = Get-RowSegments $image $y
		Assert-True ($segments -eq 3) "middle row has $segments separate objects, expected 3"

		$tan_half_fov = [Math]::Tan($vertical_fov / 2 * [Math]::PI / 180)
		$gap_x = [int]((0.75 / ($camera_distance * (Get-Aspect $image) * $tan_half_fov) + 1) / 2 * $image.Width)
		Assert-Color (Get-Pixel $image $gap_x $y) $background 1 "gap between objects at pixel $gap_x"
	}

	# Every object uses the descriptor set of its material: red bricks in the center, gray metal on the left
	materials_per_object = {
		$run = Invoke-App -Name materials_per_object -Environment @{ LAB1_CAMERA_PITCH = "0"; LAB1_OBJECTS = "3"; LAB1_LIGHTS = "directional"; LAB1_ANIMATED_SAMPLING = "0" }
		Assert-CleanRun $run
		$image = $run.Frames[30]
		$y = [int]($image.Height * 0.56) # A row of bricks, not the mortar
		$width = $image.Width

		# Slots: x = 0 bricks, x = +1.5 lava, x = -1.5 metal
		$bricks = Get-MeanColor $image $y ([int]($width * 0.47)) ([int]($width * 0.53))
		$metal = Get-MeanColor $image $y ([int]($width * 0.2)) ([int]($width * 0.26))
		Assert-True ($bricks[0] -gt 1.3 * $bricks[2]) "center object is not red: ($($bricks -join ', '))"
		Assert-True ($metal[0] -lt 1.2 * $metal[2]) "left object is not gray: ($($metal -join ', '))"
	}

	# Paused scene gives identical frames, animation moves the object between frames
	animation = {
		$still = Invoke-App -Name animation_off -Frames @(30, 90) -Environment @{ LAB1_ANIMATE = "0"; LAB1_OBJECTS = "1"; LAB1_LIGHTS = "none" }
		Assert-CleanRun $still
		$still_hashes = @(30, 90 | ForEach-Object { (Get-FileHash (Join-Path $still.Dir "$_.ppm")).Hash })
		Assert-True ($still_hashes[0] -eq $still_hashes[1]) "frames 30 and 90 differ without animation"

		$moving = Invoke-App -Name animation_on -Frames @(30, 90) -Environment @{ LAB1_ANIMATE = "1"; LAB1_OBJECTS = "1"; LAB1_LIGHTS = "none" }
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
