#version 450

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 out_color;

// struct wlr_vk_frag_texture_pcr_data
layout(push_constant, row_major) uniform UBO {
	layout(offset = 80) mat4 matrix;
	float alpha;
	float luminance_multiplier;
	float radius_top;
	float radius_bottom;
	vec4 box;
} data;

layout (constant_id = 0) const int TEXTURE_TRANSFORM = 0;

// Matches enum wlr_vk_texture_transform
#define TEXTURE_TRANSFORM_IDENTITY 0
#define TEXTURE_TRANSFORM_SRGB 1
#define TEXTURE_TRANSFORM_ST2084_PQ 2
#define TEXTURE_TRANSFORM_GAMMA22 3
#define TEXTURE_TRANSFORM_BT1886 4

float srgb_channel_to_linear(float x) {
	return mix(x / 12.92,
		pow((x + 0.055) / 1.055, 2.4),
		x > 0.04045);
}

vec3 srgb_color_to_linear(vec3 color) {
	return vec3(
		srgb_channel_to_linear(color.r),
		srgb_channel_to_linear(color.g),
		srgb_channel_to_linear(color.b)
	);
}

vec3 pq_color_to_linear(vec3 color) {
	float inv_m1 = 1 / 0.1593017578125;
	float inv_m2 = 1 / 78.84375;
	float c1 = 0.8359375;
	float c2 = 18.8515625;
	float c3 = 18.6875;
	vec3 num = max(pow(color, vec3(inv_m2)) - c1, 0);
	vec3 denom = c2 - c3 * pow(color, vec3(inv_m2));
	return pow(num / denom, vec3(inv_m1));
}

vec3 bt1886_color_to_linear(vec3 color) {
	float lb = pow(0.0001, 1.0 / 2.4);
	float lw = pow(1.0, 1.0 / 2.4);
	float a  = pow(lw - lb, 2.4);
	float b  = lb / (lw - lb);
	return a * pow(color + vec3(b), vec3(2.4));
}

float antialias(float x, float x0, float x1, float fw) {
    float xmax = max(x1, x + fw);
    float xmin = min(x0, x - fw);
    float len = xmax - xmin;
    float d0 = abs(x + fw - x1);
    float d1 = abs(x - fw - x0);
    float overlap = len - d0 - d1;
    float alpha = smoothstep(0.0, 1.0, overlap);
    return alpha;
}

float fw2(float r, vec2 p) {
    vec2 ap = abs(p);
    return 0.5 * r / max(ap.x, ap.y);
}

void main() {
	vec4 in_color = textureLod(tex, uv, 0);

	// Convert from pre-multiplied alpha to straight alpha
	float alpha = in_color.a;
	vec3 rgb;
	if (alpha == 0) {
		rgb = vec3(0);
	} else {
		rgb = in_color.rgb / alpha;
	}

	if (TEXTURE_TRANSFORM != TEXTURE_TRANSFORM_IDENTITY) {
		rgb = max(rgb, vec3(0));
	}
	if (TEXTURE_TRANSFORM == TEXTURE_TRANSFORM_SRGB) {
		rgb = srgb_color_to_linear(rgb);
	} else if (TEXTURE_TRANSFORM == TEXTURE_TRANSFORM_ST2084_PQ) {
		rgb = pq_color_to_linear(rgb);
	} else if (TEXTURE_TRANSFORM == TEXTURE_TRANSFORM_GAMMA22) {
		rgb = pow(rgb, vec3(2.2));
	} else if (TEXTURE_TRANSFORM == TEXTURE_TRANSFORM_BT1886) {
		rgb = bt1886_color_to_linear(rgb);
	}

	rgb *= data.luminance_multiplier;

	rgb = mat3(data.matrix) * rgb;

	// Back to pre-multiplied alpha
	out_color = vec4(rgb * alpha, alpha);

    if (data.radius_top > 0.0) {
        vec2 pos = vec2(gl_FragCoord);
        vec2 rel = pos.xy - data.box.xy;
        float width = data.box.z;
        if (rel.x < data.radius_top + 0.5) {
            if (rel.y < data.radius_top + 0.5) {
                vec2 p = rel - vec2(data.radius_top);
                float r = length(p);
                if (r > data.radius_top - 1.0) {
                    float opacity = antialias(r, data.radius_top - 1.0, data.radius_top, fw2(r, p));
                    out_color *= data.alpha * opacity;
                    return;
                }
            }
        } else if (rel.x > width - (data.radius_top + 0.5)) {
            if (rel.y < data.radius_top + 0.5) {
                vec2 p = rel - vec2(width - data.radius_top, data.radius_top);
                float r = length(p);
                if (r > data.radius_top - 1.0) {
                    float opacity = antialias(r, data.radius_top - 1.0, data.radius_top, fw2(r, p));
                    out_color *= data.alpha * opacity;
                    return;
                }
            }
        }
    }
    if (data.radius_bottom > 0.0) {
        vec2 pos = vec2(gl_FragCoord);
        vec2 rel = pos.xy - data.box.xy;
        float width = data.box.z;
        float height = data.box.w;
        if (rel.x < data.radius_bottom + 0.5) {
            if (rel.y > height - (data.radius_bottom + 0.5)) {
                vec2 p = rel - vec2(data.radius_bottom, height - data.radius_bottom);
                float r = length(p);
                if (r > data.radius_bottom - 1.0) {
                    float opacity = antialias(r, data.radius_bottom - 1.0, data.radius_bottom, fw2(r, p));
                    out_color *= data.alpha * opacity;
                    return;
                }
            }
        } else if (rel.x > width - (data.radius_bottom + 0.5)) {
            if (rel.y > height - (data.radius_bottom + 0.5)) {
                vec2 p = rel - vec2(width - data.radius_bottom, height - data.radius_bottom);
                float r = length(p);
                if (r > data.radius_bottom - 1.0) {
                    float opacity = antialias(r, data.radius_bottom - 1.0, data.radius_bottom, fw2(r, p));
                    out_color *= data.alpha * opacity;
                    return;
                }
            }
        }
    }

	out_color *= data.alpha;
}
