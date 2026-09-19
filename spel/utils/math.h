#ifndef SPEL_MATH
#define SPEL_MATH
#include "core/macros.h"
#include <math.h>
#include <stdint.h>

#define spel_e 2.71828182845904523536
#define spel_log2e 1.44269504088896340736
#define spel_log10e 0.43429448190325182765
#define spel_ln2 0.69314718055994530942
#define spel_ln10 2.30258509299404568402
#define spel_pi 3.14159265358979323846
#define spel_pi_2 1.57079632679489661923
#define spel_pi_4 0.78539816339744830962
#define spel_1_pi 0.31830988618379067154
#define spel_2_pi 0.63661977236758134308
#define spel_sqrt2 1.41421356237309504880
#define spel_sqrt1_2 0.70710678118654752440

#define spel_inf (1.0 / 0.0)
#define spel_nan (0.0 / 0.0)
#define spel_inf_f (1.0F / 0.0F)
#define spel_nan_f (0.0F / 0.0F)

#define spel_tau 6.28318530717958647692
#define spel_deg2rad (spel_pi / 180.0)
#define spel_rad2deg (180.0 / spel_pi)

#define spel_epsilon 1e-15
#define spel_epsilonf 1e-6F
#define spel_neg_inf (-1.0 / 0.0)
#define spel_neg_inf_f (-1.0F / 0.0F)

#define spel_e 2.71828182845904523536
#define spel_sqrt3 1.73205080756887729352

#define spel_golden_ratio 1.61803398874989484820
#define spel_silver_ratio 2.41421356237309504880
#define spel_bronze_ratio 1.32471795724474602596
#define spel_golden_angle_rad 2.39996322972865332223
#define spel_golden_angle_deg 137.507764050037854646

#define spel_sqrt5 2.23606797749978969640
#define spel_fourth_root_two 1.18920711500272106672
#define spel_sqrt_pi 1.77245385090551602729

#define spel_eps_angle 0.0001
#define spel_unit_xy_angle 0.78539816339744830962 /* 45° */

#define spel_log2_10 3.32192809488736234787
#define spel_log10_2 0.30102999566398119521

#define spel_gamma_half 1.77245385090551602729 /* sqrt(pi) */

#define spel_int32_max 2147483647
#define spel_int32_min (-2147483648)
#define spel_int64_max 9223372036854775807LL
#define spel_int64_min (-9223372036854775808LL)

#define spel_fnv_offset_basis 1469598103934665603ULL
#define spel_fnv_prime 1099511628211ULL
#define spel_murmur_c1 0xcc9e2d51
#define spel_murmur_c2 0x1b873593

#define spel_standard_gravity 9.80665
#define spel_speed_of_light 299792458.0
#define spel_human_reaction_time_ms 250

#define spel_feigenbaum_delta 4.66920160910299067185
#define spel_feigenbaum_alpha 2.50290787509589282228
#define spel_khinchin_constant 2.68545200106530644531
#define spel_apery_constant 1.20205690315959428540
#define spel_euler_mascheroni 0.57721566490153286060
#define spel_catalan_constant 0.91596559417721901505

#define spel_inv_sqrt_pi 0.39894228040143267794
#define spel_gaussian_norm 0.39894228040143267794 /* same thing */

#define spel_circle_area_coeff spel_pi
#define spel_sphere_volume_coeff 4.18879020478639098462	  /* 4*pi/3 */
#define spel_sphere_surface_coeff 12.56637061435917295385 /* 4*pi */
#define spel_deg120 2.09439510239319549231				  /* 2*pi/3 */
#define spel_deg60 1.04719755119659774615				  /* pi/3 */

#define spel_plastic_constant 1.32471795724474602596
#define spel_landau_ramanujan_const 0.76422365358922066299
#define spel_viswanath_constant 1.1319882487943
#define spel_glivenko_cantelli_const 0.409
#define spel_li2_pi2 0.822467033024 /* pi^2/12 */
#define spel_ln_pi 1.14472988584940017414
#define spel_ln_ln_2 (-0.3665129205816643)

#define spel_artin_constant 0.37395581361920228805
#define spel_primorial_e_constant 1.943596436813827

typedef struct
{
	uint8_t r, g, b, a;
} spel_color;

typedef struct
{
	float x, y;
} spel_vec2;
typedef struct
{
	float x, y, z;
} spel_vec3;
typedef struct
{
	float x, y, z, w;
} spel_vec4;

typedef struct
{
	int x, y, width, height;
} spel_rect;

typedef struct
{
	float m[16];
} spel_mat4;

typedef struct
{
	float m[4];
} spel_mat2;

typedef struct
{
	float m[9];
} spel_mat3;

typedef struct
{
	float x, y, z, w;
} spel_quat;

typedef struct
{
	spel_vec3 min, max;
} spel_aabb;

typedef struct
{
	spel_vec2 center;
	float radius;
} spel_circle;

typedef struct
{
	spel_vec3 origin, direction;
} spel_ray;

typedef struct
{
	spel_vec3 normal;
	float d;
} spel_plane;

#define spel_color_declare(name) spel_api extern spel_color spel_color_##name

spel_color_declare(black);
spel_color_declare(white);
spel_color_declare(red);
spel_color_declare(green);
spel_color_declare(blue);
spel_color_declare(cyan);
spel_color_declare(magenta);
spel_color_declare(yellow);
spel_color_declare(orange);
spel_color_declare(purple);
spel_color_declare(pink);
spel_color_declare(brown);
spel_color_declare(gray);
spel_color_declare(dark_gray);
spel_color_declare(light_gray);
spel_color_declare(transparent);
spel_color_declare(sky);

#define spel_color_rgba(r, g, b, a) ((spel_color){(r), (g), (b), (a)})
#define spel_color_rgb(r, g, b) ((spel_color){(r), (g), (b), 255})
#define spel_color_hex(hex)                                                              \
	((spel_color){((hex) >> 16) & 0xFF, ((hex) >> 8) & 0xFF, ((hex)) & 0xFF, 255})
#define spel_color_hexa(hex)                                                             \
	((spel_color){((hex) >> 24) & 0xFF, ((hex) >> 16) & 0xFF, ((hex) >> 8) & 0xFF,       \
				  ((hex)) & 0xFF})

spel_api int spel_math_isnan(double x);
spel_api int spel_math_isinf(double x);
spel_api int spel_math_isfinite(double x);

spel_api float spel_math_sqrt(float x);
spel_api float spel_math_rsqrt(float x);
spel_api float spel_math_pow(float base, float exp);
spel_api float spel_math_log(float x);
spel_api float spel_math_log2(float x);
spel_api float spel_math_exp(float x);

spel_api float spel_math_sin(float x);
spel_api float spel_math_cos(float x);
spel_api float spel_math_tan(float x);
spel_api float spel_math_asin(float x);
spel_api float spel_math_acos(float x);
spel_api float spel_math_atan(float x);
spel_api float spel_math_atan2(float y, float x);

spel_api float spel_math_floor(float x);
spel_api float spel_math_ceil(float x);
spel_api float spel_math_round(float x);
spel_api float spel_math_fmod(float x, float y);
spel_api float spel_math_wrap(float x, float min, float max);
spel_api float spel_math_snap(float x, float step);

spel_api float spel_math_lerp_clamped(float a, float b, float t);
spel_api float spel_math_inverse_lerp(float a, float b, float v);
spel_api float spel_math_remap(float v, float a0, float b0, float a1, float b1);
spel_api float spel_math_smootherstep(float edge0, float edge1, float x);

spel_api float spel_math_angle_wrap(float radians);
spel_api float spel_math_angle_diff(float a, float b);
spel_api float spel_math_angle_lerp(float a, float b, float t);

spel_api int spel_math_next_pow2(int x);
spel_api int spel_maths_pow2(int x);
spel_api int spel_math_gcd(int a, int b);
spel_api int spel_math_lcm(int a, int b);
spel_api float spel_math_approach(float current, float target, float step);

spel_api float spel_math_maxf(float a, float b);
spel_api float spel_math_minf(float a, float b);

#define spel_vec2(x, y) ((spel_vec2){(x), (y)})
#define spel_vec2_zero ((spel_vec2){0.0f, 0.0f})
#define spel_vec2_one ((spel_vec2){1.0f, 1.0f})
#define spel_vec2_up ((spel_vec2){0.0f, 1.0f})
#define spel_vec2_down ((spel_vec2){0.0f, -1.0f})
#define spel_vec2_left ((spel_vec2){-1.0f, 0.0f})
#define spel_vec2_right ((spel_vec2){1.0f, 0.0f})

spel_api spel_vec2 spel_vec2_mul(spel_vec2 a, spel_vec2 b);
spel_api spel_vec2 spel_vec2_div(spel_vec2 a, spel_vec2 b);
spel_api spel_vec2 spel_vec2_neg(spel_vec2 v);

spel_api float spel_vec2_cross(spel_vec2 a, spel_vec2 b);
spel_api float spel_vec2_dist(spel_vec2 a, spel_vec2 b);
spel_api float spel_vec2_dist_sq(spel_vec2 a, spel_vec2 b);
spel_api float spel_vec2_angle(spel_vec2 v);
spel_api float spel_vec2_angle_between(spel_vec2 a, spel_vec2 b);

spel_api spel_vec2 spel_vec2_normalize_safe(spel_vec2 v, spel_vec2 fallback);
spel_api spel_vec2 spel_vec2_lerp(spel_vec2 a, spel_vec2 b, float t);
spel_api spel_vec2 spel_vec2_clamp_len(spel_vec2 v, float maxLen);
spel_api spel_vec2 spel_vec2_clamp(spel_vec2 v, spel_vec2 lo, spel_vec2 hi);
spel_api spel_vec2 spel_vec2_min(spel_vec2 a, spel_vec2 b);
spel_api spel_vec2 spel_vec2_max(spel_vec2 a, spel_vec2 b);
spel_api spel_vec2 spel_vec2_abs(spel_vec2 v);
spel_api spel_vec2 spel_vec2_floor(spel_vec2 v);
spel_api spel_vec2 spel_vec2_ceil(spel_vec2 v);
spel_api spel_vec2 spel_vec2_round(spel_vec2 v);
spel_api spel_vec2 spel_vec2_perpendicular(spel_vec2 v);
spel_api spel_vec2 spel_vec2_reflect(spel_vec2 v, spel_vec2 n);
spel_api spel_vec2 spel_vec2_project(spel_vec2 v, spel_vec2 onto);
spel_api spel_vec2 spel_vec2_rotate(spel_vec2 v, float radians);
spel_api spel_vec2 spel_vec2_from_angle(float radians);
spel_api spel_vec2 spel_vec2_approach(spel_vec2 current, spel_vec2 target, float step);

spel_api int spel_vec2_eq(spel_vec2 a, spel_vec2 b);
spel_api int spel_vec2_nearly_eq(spel_vec2 a, spel_vec2 b, float eps);

#define spel_vec3(x, y, z) ((spel_vec3){(x), (y), (z)})
#define spel_vec3_zero ((spel_vec3){0.0f, 0.0f, 0.0f})
#define spel_vec3_one ((spel_vec3){1.0f, 1.0f, 1.0f})
#define spel_vec3_up ((spel_vec3){0.0f, 1.0f, 0.0f})
#define spel_vec3_down ((spel_vec3){0.0f, -1.0f, 0.0f})
#define spel_vec3_left ((spel_vec3){-1.0f, 0.0f, 0.0f})
#define spel_vec3_right ((spel_vec3){1.0f, 0.0f, 0.0f})
#define spel_vec3_forward ((spel_vec3){0.0f, 0.0f, -1.0f})
#define spel_vec3_back ((spel_vec3){0.0f, 0.0f, 1.0f})

spel_api spel_vec3 spel_vec3_mul(spel_vec3 a, spel_vec3 b);
spel_api spel_vec3 spel_vec3_div(spel_vec3 a, spel_vec3 b);
spel_api spel_vec3 spel_vec3_neg(spel_vec3 v);

spel_api float spel_vec3_dist(spel_vec3 a, spel_vec3 b);
spel_api float spel_vec3_dist_sq(spel_vec3 a, spel_vec3 b);
spel_api float spel_vec3_angle_between(spel_vec3 a, spel_vec3 b);

spel_api spel_vec3 spel_vec3_normalize_safe(spel_vec3 v, spel_vec3 fallback);
spel_api spel_vec3 spel_vec3_lerp(spel_vec3 a, spel_vec3 b, float t);
spel_api spel_vec3 spel_vec3_slerp(spel_vec3 a, spel_vec3 b, float t);
spel_api spel_vec3 spel_vec3_clamp_len(spel_vec3 v, float maxLen);
spel_api spel_vec3 spel_vec3_clamp(spel_vec3 v, spel_vec3 lo, spel_vec3 hi);
spel_api spel_vec3 spel_vec3_min(spel_vec3 a, spel_vec3 b);
spel_api spel_vec3 spel_vec3_max(spel_vec3 a, spel_vec3 b);
spel_api spel_vec3 spel_vec3_abs(spel_vec3 v);
spel_api spel_vec3 spel_vec3_floor(spel_vec3 v);
spel_api spel_vec3 spel_vec3_ceil(spel_vec3 v);
spel_api spel_vec3 spel_vec3_round(spel_vec3 v);
spel_api spel_vec3 spel_vec3_reflect(spel_vec3 v, spel_vec3 n);
spel_api spel_vec3 spel_vec3_refract(spel_vec3 v, spel_vec3 n, float eta);
spel_api spel_vec3 spel_vec3_project(spel_vec3 v, spel_vec3 onto);
spel_api spel_vec3 spel_vec3_project_plane(spel_vec3 v, spel_vec3 normal);
spel_api spel_vec3 spel_vec3_approach(spel_vec3 current, spel_vec3 target, float step);

spel_api spel_vec2 spel_vec3_xy(spel_vec3 v);
spel_api spel_vec2 spel_vec3_xz(spel_vec3 v);

spel_api int spel_vec3_eq(spel_vec3 a, spel_vec3 b);
spel_api int spel_vec3_nearly_eq(spel_vec3 a, spel_vec3 b, float eps);

#define spel_vec4(x, y, z, w) ((spel_vec4){(x), (y), (z), (w)})
#define spel_vec4_zero ((spel_vec4){0.0f, 0.0f, 0.0f, 0.0f})
#define spel_vec4_one ((spel_vec4){1.0f, 1.0f, 1.0f, 1.0f})

spel_api spel_vec4 spel_vec4_add(spel_vec4 a, spel_vec4 b);
spel_api spel_vec4 spel_vec4_sub(spel_vec4 a, spel_vec4 b);
spel_api spel_vec4 spel_vec4_mul(spel_vec4 a, spel_vec4 b);
spel_api spel_vec4 spel_vec4_div(spel_vec4 a, spel_vec4 b);
spel_api spel_vec4 spel_vec4_scale(spel_vec4 v, float s);
spel_api spel_vec4 spel_vec4_neg(spel_vec4 v);

spel_api float spel_vec4_dot(spel_vec4 a, spel_vec4 b);
spel_api float spel_vec4_len(spel_vec4 v);
spel_api float spel_vec4_len_sq(spel_vec4 v);

spel_api spel_vec4 spel_vec4_normalize(spel_vec4 v);
spel_api spel_vec4 spel_vec4_lerp(spel_vec4 a, spel_vec4 b, float t);
spel_api spel_vec4 spel_vec4_min(spel_vec4 a, spel_vec4 b);
spel_api spel_vec4 spel_vec4_max(spel_vec4 a, spel_vec4 b);
spel_api spel_vec4 spel_vec4_abs(spel_vec4 v);

spel_api spel_vec3 spel_vec4_xyz(spel_vec4 v);
spel_api spel_vec2 spel_vec4_xy(spel_vec4 v);

spel_api int spel_vec4_eq(spel_vec4 a, spel_vec4 b);
spel_api int spel_vec4_nearly_eq(spel_vec4 a, spel_vec4 b, float eps);

spel_api spel_mat4 spel_mat4_identity(void);
spel_api spel_mat4 spel_mat4_zero(void);

spel_api spel_mat4 spel_mat4_mul(spel_mat4 a, spel_mat4 b);
spel_api spel_vec4 spel_mat4_mul_vec4(spel_mat4 m, spel_vec4 v);
spel_api spel_vec3 spel_mat4_mul_vec3(spel_mat4 m, spel_vec3 v, float w);

spel_api spel_mat4 spel_mat4_transpose(spel_mat4 m);
spel_api spel_mat4 spel_mat4_inverse(spel_mat4 m);
spel_api float spel_mat4_determinant(spel_mat4 m);

spel_api spel_mat4 spel_mat4_translate(float x, float y, float z);
spel_api spel_mat4 spel_mat4_translate_v(spel_vec3 v);
spel_api spel_mat4 spel_mat4_scale(float x, float y, float z);
spel_api spel_mat4 spel_mat4_scale_v(spel_vec3 v);
spel_api spel_mat4 spel_mat4_scale_uniform(float s);
spel_api spel_mat4 spel_mat4_rotate_x(float radians);
spel_api spel_mat4 spel_mat4_rotate_y(float radians);
spel_api spel_mat4 spel_mat4_rotate_z(float radians);
spel_api spel_mat4 spel_mat4_rotate_axis(spel_vec3 axis, float radians);
spel_api spel_mat4 spel_mat4_from_quat(spel_quat q);
spel_api spel_mat4 spel_mat4_trs(spel_vec3 t, spel_quat r, spel_vec3 s);

spel_api spel_mat4 spel_mat4_ortho(float left, float right, float bottom, float top,
								   float near, float far);
spel_api spel_mat4 spel_mat4_ortho_2d(float width, float height);
spel_api spel_mat4 spel_mat4_perspective(float fovY, float aspect, float near, float far);
spel_api spel_mat4 spel_mat4_perspective_fov(float fovY, float width, float height,
											 float near, float far);
spel_api spel_mat4 spel_mat4_frustum(float left, float right, float bottom, float top,
									 float near, float far);

spel_api spel_mat4 spel_mat4_look_at(spel_vec3 eye, spel_vec3 target, spel_vec3 up);

spel_api void spel_mat4_decompose(spel_mat4 m, spel_vec3* outTranslation,
								  spel_quat* outRotation, spel_vec3* outScale);
spel_api spel_vec3 spel_mat4_get_translation(spel_mat4 m);
spel_api spel_vec3 spel_mat4_get_scale(spel_mat4 m);

spel_api int spel_mat4_eq(spel_mat4 a, spel_mat4 b);
spel_api int spel_mat4_nearly_eq(spel_mat4 a, spel_mat4 b, float eps);

spel_api spel_mat3 spel_mat3_identity(void);
spel_api spel_mat3 spel_mat3_zero(void);
spel_api spel_mat3 spel_mat3_mul(spel_mat3 a, spel_mat3 b);
spel_api spel_vec3 spel_mat3_mul_vec3(spel_mat3 m, spel_vec3 v);
spel_api spel_mat3 spel_mat3_transpose(spel_mat3 m);
spel_api spel_mat3 spel_mat3_inverse(spel_mat3 m);
spel_api float spel_mat3_determinant(spel_mat3 m);
spel_api spel_mat3 spel_mat3_from_mat4(spel_mat4 m);
spel_api spel_mat3 spel_mat3_normal_matrix(spel_mat4 model);
spel_api spel_mat3 spel_mat3_rotate(float radians);

spel_api spel_mat2 spel_mat2_identity(void);
spel_api spel_mat2 spel_mat2_zero(void);
spel_api spel_mat2 spel_mat2_mul(spel_mat2 a, spel_mat2 b);
spel_api spel_vec2 spel_mat2_mul_vec2(spel_mat2 m, spel_vec2 v);
spel_api spel_mat2 spel_mat2_transpose(spel_mat2 m);
spel_api spel_mat2 spel_mat2_inverse(spel_mat2 m);
spel_api float spel_mat2_determinant(spel_mat2 m);
spel_api spel_mat2 spel_mat2_rotate(float radians);
spel_api spel_vec2 spel_mat3_transform_point(spel_mat3 m, spel_vec2 p);

spel_api spel_quat spel_quat_identity(void);
spel_api spel_quat spel_quat_from_axis_angle(spel_vec3 axis, float radians);
spel_api spel_quat spel_quat_from_euler(float pitch, float yaw, float roll);
spel_api spel_quat spel_quat_from_mat4(spel_mat4 m);
spel_api spel_quat spel_quat_from_mat3(spel_mat3 m);
spel_api spel_quat spel_quat_from_to(spel_vec3 from, spel_vec3 to);
spel_api spel_quat spel_quat_look_at(spel_vec3 forward, spel_vec3 up);

spel_api spel_quat spel_quat_mul(spel_quat a, spel_quat b);
spel_api spel_vec3 spel_quat_mul_vec3(spel_quat q, spel_vec3 v);
spel_api spel_quat spel_quat_conjugate(spel_quat q);
spel_api spel_quat spel_quat_inverse(spel_quat q);
spel_api float spel_quat_dot(spel_quat a, spel_quat b);
spel_api float spel_quat_len(spel_quat q);
spel_api float spel_quat_len_sq(spel_quat q);
spel_api spel_quat spel_quat_normalize(spel_quat q);
spel_api spel_quat spel_quat_nlerp(spel_quat a, spel_quat b, float t);
spel_api spel_quat spel_quat_slerp(spel_quat a, spel_quat b, float t);

spel_api void spel_quat_to_axis_angle(spel_quat q, spel_vec3* outAxis, float* outRadians);
spel_api spel_vec3 spel_quat_to_euler(spel_quat q);
spel_api spel_mat4 spel_quat_to_mat4(spel_quat q);

spel_api int spel_quat_eq(spel_quat a, spel_quat b);
spel_api int spel_quat_nearly_eq(spel_quat a, spel_quat b, float eps);

#define spel_rect(x, y, w, h) ((spel_rect){(x), (y), (w), (h)})

spel_api spel_rect spel_rect_create(int x, int y, int w, int h);
spel_api spel_rect spel_rect_from_points(int x0, int y0, int x1, int y1);

spel_api int spel_rect_contains_point(spel_rect r, int x, int y);
spel_api int spel_rect_contains_rect(spel_rect outer, spel_rect inner);
spel_api int spel_rect_intersects(spel_rect a, spel_rect b);
spel_api spel_rect spel_rect_intersection(spel_rect a, spel_rect b);
spel_api spel_rect spel_rect_union(spel_rect a, spel_rect b);
spel_api spel_rect spel_rect_expand(spel_rect r, int amount);
spel_api spel_rect spel_rect_shrink(spel_rect r, int amount);
spel_api spel_rect spel_rect_translate(spel_rect r, int dx, int dy);
spel_api spel_rect spel_rect_center_inn(spel_rect inner, spel_rect outer);

spel_api int spel_rect_right(spel_rect r);
spel_api int spel_rect_bottom(spel_rect r);
spel_api spel_vec2 spel_rect_center(spel_rect r);
spel_api spel_vec2 spel_rect_size(spel_rect r);

spel_api int spel_rect_eq(spel_rect a, spel_rect b);

spel_api spel_aabb spel_aabb_create(spel_vec3 min, spel_vec3 max);
spel_api spel_aabb spel_aabb_from_center(spel_vec3 center, spel_vec3 halfExtents);

spel_api spel_vec3 spel_aabb_center(spel_aabb b);
spel_api spel_vec3 spel_aabb_extents(spel_aabb b);
spel_api spel_vec3 spel_aabb_size(spel_aabb b);

spel_api int spel_aabb_contains_point(spel_aabb b, spel_vec3 p);
spel_api int spel_aabb_intersects(spel_aabb a, spel_aabb b);
spel_api int spel_aabb_intersects_circle(spel_aabb a, spel_circle circle);
spel_api spel_aabb spel_aabb_union(spel_aabb a, spel_aabb b);
spel_api spel_aabb spel_aabb_expand(spel_aabb b, spel_vec3 amount);
spel_api spel_aabb spel_aabb_transform(spel_aabb b, spel_mat4 m);

spel_api int spel_circle_contains_point(spel_circle c, spel_vec2 p);
spel_api int spel_circle_intersects_circle(spel_circle a, spel_circle b);
spel_api int spel_circle_intersects_rect(spel_circle c, spel_rect r);

spel_api spel_vec3 spel_ray_at(spel_ray ray, float t);
spel_api int spel_ray_intersects_aabb(spel_ray ray, spel_aabb box, float* outT);
spel_api int spel_ray_intersects_plane(spel_ray ray, spel_plane plane, float* outT);
spel_api int spel_ray_intersects_sphere(spel_ray ray, spel_vec3 center, float radius,
										float* outT);
spel_api int spel_ray_intersects_triangle(spel_ray ray, spel_vec3 v0, spel_vec3 v1,
										  spel_vec3 v2, float* outT);

spel_api spel_plane spel_plane_create(spel_vec3 normal, spel_vec3 point);
spel_api float spel_plane_dist_to_point(spel_plane p, spel_vec3 point);
spel_api spel_vec3 spel_plane_project_point(spel_plane p, spel_vec3 point);
spel_api int spel_plane_side(spel_plane p, spel_vec3 point);

spel_api spel_color spel_color_hsl(int h, float s, float l, uint8_t a);
spel_api spel_color spel_color_hsv(int h, float s, float v, uint8_t a);
spel_api spel_color spel_color_mix(spel_color a, spel_color b, float t);
spel_api spel_color spel_color_invert(spel_color c);
spel_api spel_color spel_color_alpha(spel_color c, uint8_t a);
spel_api spel_color spel_color_darken(spel_color c, float amount);
spel_api spel_color spel_color_lighten(spel_color c, float amount);
spel_api spel_color spel_color_saturate(spel_color c, float amount);
spel_api spel_color spel_color_desaturate(spel_color c, float amount);
spel_api spel_color spel_color_grayscale(spel_color c);
spel_api spel_color spel_color_premultiply_alpha(spel_color c);

spel_api spel_vec4 spel_color_to_vec4(spel_color c);
spel_api spel_color spel_color_from_vec4(spel_vec4 v);
spel_api uint32_t spel_color_to_u32_rgba(spel_color c);
spel_api uint32_t spel_color_to_u32_argb(spel_color c);
spel_api spel_color spel_color_from_u32_rgba(uint32_t v);
spel_api float* spel_color_array(spel_color c);

spel_api float spel_ease_linear(float t);

spel_api float spel_ease_in_sine(float t);
spel_api float spel_ease_out_sine(float t);
spel_api float spel_ease_in_out_sine(float t);

spel_api float spel_ease_in_quad(float t);
spel_api float spel_ease_out_quad(float t);
spel_api float spel_ease_in_out_quad(float t);

spel_api float spel_ease_in_cubic(float t);
spel_api float spel_ease_out_cubic(float t);
spel_api float spel_ease_in_out_cubic(float t);

spel_api float spel_ease_in_quart(float t);
spel_api float spel_ease_out_quart(float t);
spel_api float spel_ease_in_out_quart(float t);

spel_api float spel_ease_in_quint(float t);
spel_api float spel_ease_out_quint(float t);
spel_api float spel_ease_in_out_quint(float t);

spel_api float spel_ease_in_expo(float t);
spel_api float spel_ease_out_expo(float t);
spel_api float spel_ease_in_out_expo(float t);

spel_api float spel_ease_in_circ(float t);
spel_api float spel_ease_out_circ(float t);
spel_api float spel_ease_in_out_circ(float t);

spel_api float spel_ease_in_back(float t);
spel_api float spel_ease_out_back(float t);
spel_api float spel_ease_in_out_back(float t);

spel_api float spel_ease_in_elastic(float t);
spel_api float spel_ease_out_elastic(float t);
spel_api float spel_ease_in_out_elastic(float t);

spel_api float spel_ease_in_bounce(float t);
spel_api float spel_ease_out_bounce(float t);
spel_api float spel_ease_in_out_bounce(float t);

typedef struct
{
	uint64_t state;
} spel_rng;

spel_api spel_rng spel_rng_create(uint64_t seed);
spel_api uint32_t spel_rng_next(spel_rng* rng);
spel_api float spel_rng_float(spel_rng* rng);
spel_api float spel_rng_float_range(spel_rng* rng, float lo, float hi);
spel_api int spel_rngnt_range(spel_rng* rng, int lo, int hi);
spel_api spel_vec2 spel_rng_vec2_unit(spel_rng* rng);
spel_api spel_vec3 spel_rng_vec3_unit(spel_rng* rng);
spel_api spel_color spel_rng_color(spel_rng* rng, uint8_t alpha);

spel_api float spel_noise_value(float x);
spel_api float spel_noise_value2(float x, float y);
spel_api float spel_noise_perlin(float x, float y);
spel_api float spel_noise_fbm(float x, float y, int octaves, float lacunarity,
							  float gain);

static inline float spel_math_deg2rad(float d)
{
	return d * (spel_pi / 180.0F);
}
static inline float spel_math_rad2deg(float r)
{
	return r * (180.0F / spel_pi);
}
static inline float spel_math_lerp(float a, float b, float t)
{
	return a + ((b - a) * t);
}
static inline float spel_math_clamp(float v, float lo, float hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}
static inline float spel_math_smoothstep(float e0, float e1, float x)
{
	float t = spel_math_clamp((x - e0) / (e1 - e0), 0.0F, 1.0F);
	return t * t * (3.0F - (2.0F * t));
}

static inline spel_vec2 spel_vec2_add(spel_vec2 a, spel_vec2 b)
{
	return spel_vec2(a.x + b.x, a.y + b.y);
}
static inline spel_vec2 spel_vec2_sub(spel_vec2 a, spel_vec2 b)
{
	return spel_vec2(a.x - b.x, a.y - b.y);
}
static inline spel_vec2 spel_vec2_scale(spel_vec2 v, float s)
{
	return spel_vec2(v.x * s, v.y * s);
}
static inline float spel_vec2_dot(spel_vec2 a, spel_vec2 b)
{
	return (a.x * b.x) + (a.y * b.y);
}
static inline float spel_vec2_len_sq(spel_vec2 v)
{
	return (v.x * v.x) + (v.y * v.y);
}
static inline float spel_vec2_len(spel_vec2 v)
{
	return sqrtf(spel_vec2_len_sq(v));
}
static inline spel_vec2 spel_vec2_normalize(spel_vec2 v)
{
	float l = spel_vec2_len(v);
	return l > spel_epsilon ? spel_vec2_scale(v, 1.0F / l) : spel_vec2_zero;
}

static inline spel_vec3 spel_vec3_add(spel_vec3 a, spel_vec3 b)
{
	return spel_vec3(a.x + b.x, a.y + b.y, a.z + b.z);
}
static inline spel_vec3 spel_vec3_sub(spel_vec3 a, spel_vec3 b)
{
	return spel_vec3(a.x - b.x, a.y - b.y, a.z - b.z);
}
static inline spel_vec3 spel_vec3_scale(spel_vec3 v, float s)
{
	return spel_vec3(v.x * s, v.y * s, v.z * s);
}
static inline float spel_vec3_dot(spel_vec3 a, spel_vec3 b)
{
	return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}
static inline float spel_vec3_len_sq(spel_vec3 v)
{
	return (v.x * v.x) + (v.y * v.y) + (v.z * v.z);
}
static inline float spel_vec3_len(spel_vec3 v)
{
	return sqrtf(spel_vec3_len_sq(v));
}
static inline spel_vec3 spel_vec3_cross(spel_vec3 a, spel_vec3 b)
{
	return spel_vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline spel_vec3 spel_vec3_normalize(spel_vec3 v)
{
	float l = spel_vec3_len(v);
	return l > spel_epsilon ? spel_vec3_scale(v, 1.0F / l) : spel_vec3_zero;
}

#endif