#version 450

layout(location = 0) out vec4 out_color;
layout(binding = 0) uniform UBO {
	bool enabled;
	float blur;
	float radius_top;
	float radius_bottom;
	vec4 box;
	vec4 color;
} data;

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

// https://gitlab.gnome.org/GNOME/gtk/-/blob/gtk-4-16/gsk/gpu/shaders/gskgpuboxshadow.glsl
#define PI 3.14159265358979323846
#define SQRT1_2 0.70710678118654752440

/* A standard gaussian function, used for weighting samples */
float gauss(float x, float sigma) {
  float sigma_2 = sigma * sigma;
  return 1.0 / sqrt(2.0 * PI * sigma_2) * exp(-(x * x) / (2.0 * sigma_2));
}

/* This approximates the error function, needed for the gaussian integral */
vec2 erf(vec2 x) {
  vec2 s = sign(x), a = abs(x);
  x = 1.0 + (0.278393 + (0.230389 + 0.078108 * (a * a)) * a) * a;
  x *= x;
  return s - s / (x * x);
}

float erf_range(vec2 x, float sigma) {
  vec2 from_to = 0.5 - 0.5 * erf (x / (sigma * SQRT1_2));
  return from_to.y - from_to.x;
}

float ellipse_x(vec2 ellipse, float y) {
  float y_scaled = y / ellipse.y;
  return ellipse.x * sqrt(1.0 - y_scaled * y_scaled);
}

float blur_rect(vec4 r, vec2 pos, float sigma) {
  return erf_range(vec2(r.x - pos.x, r.x + r.z - pos.x), sigma) * erf_range(vec2(r.y - pos.y, r.y + r.w - pos.y), sigma);
}

float blur_corner(vec2 p, vec2 r, float sigma) {
  if (min (r.x, r.y) <= 0.0)
    return 0.0;

  p /= sigma;
  r /= sigma;

  if (min (p.x, p.y) <= -2.95 ||
      max (p.x - r.x, p.y - r.y) >= 2.95)
    return 0.0;

  float result = 0.0;
  float start = max(p.y - 3.0, 0.0);
  float end = min(p.y + 3.0, r.y);
  float step = (end - start) / 7.0;
  float y = start;
  for (int i = 0; i < 8; i++) {
      float x = r.x - ellipse_x(r, r.y - y);
      result -= gauss(p.y - y, 1.0) * erf_range(vec2 (- p.x, x - p.x), 1.0);
      y += step;
    }
  return step * result;
}

float blur_rounded_rect (vec4 r, vec2 p, float sigma) {
  float result = blur_rect(r, p, sigma);
  result -= blur_corner(p - r.xy, vec2(data.radius_top, data.radius_top), sigma);
  result -= blur_corner(vec2(r.x + r.z - p.x, p.y - r.y), vec2(data.radius_top, data.radius_top), sigma);
  result -= blur_corner(vec2(r.x + r.z, r.y + r.w) - p, vec2(data.radius_bottom, data.radius_bottom), sigma);
  result -= blur_corner(vec2(p.x - r.x, r.y + r.w - p.y), vec2(data.radius_bottom, data.radius_bottom), sigma);

  return result;
}

void main() {
	if (!data.enabled) {
		discard;
	}
    if (data.radius_top > 0.0) {
        vec2 pos = vec2(gl_FragCoord);
        vec2 rel = pos.xy - data.box.xy;
        float width = data.box.z;
        if (rel.x < data.radius_top + 0.5) {
            if (rel.y < data.radius_top + 0.5) {
                vec2 p = rel - vec2(data.radius_top);
                float r = length(p);
                if (r > data.radius_top - 1.0) {
                    float opacity;
					if (data.blur > 0.0) {
						vec4 b = vec4(data.box.x + data.blur, data.box.y + data.blur, data.box.z -
								2.0 * data.blur, data.box.w - 2.0 *	data.blur);
						opacity = blur_rounded_rect(b, pos, data.blur);
					} else {
						opacity = antialias(r, data.radius_top - 1.0, data.radius_top, fw2(r, p));
					}
                    out_color = data.color * opacity;
                    return;
                }
            }
        } else if (rel.x > width - (data.radius_top + 0.5)) {
            if (rel.y < data.radius_top + 0.5) {
                vec2 p = rel - vec2(width - data.radius_top, data.radius_top);
                float r = length(p);
                if (r > data.radius_top - 1.0) {
                    float opacity;
					if (data.blur > 0.0) {
						vec4 b = vec4(data.box.x + data.blur, data.box.y + data.blur, data.box.z -
								2.0 * data.blur, data.box.w - 2.0 *	data.blur);
						opacity = blur_rounded_rect(b, pos, data.blur);
					} else {
						opacity = antialias(r, data.radius_top - 1.0, data.radius_top, fw2(r, p));
					}
                    out_color = data.color * opacity;
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
                    float opacity;
					if (data.blur > 0.0) {
						vec4 b = vec4(data.box.x + data.blur, data.box.y + data.blur, data.box.z -
								2.0 * data.blur, data.box.w - 2.0 *	data.blur);
						opacity = blur_rounded_rect(b, pos, data.blur);
					} else {
						opacity = antialias(r, data.radius_bottom - 1.0, data.radius_bottom, fw2(r, p));
					}
                    out_color = data.color * opacity;
                    return;
                }
            }
        } else if (rel.x > width - (data.radius_bottom + 0.5)) {
            if (rel.y > height - (data.radius_bottom + 0.5)) {
                vec2 p = rel - vec2(width - data.radius_bottom, height - data.radius_bottom);
                float r = length(p);
                if (r > data.radius_bottom - 1.0) {
                    float opacity;
					if (data.blur > 0.0) {
						vec4 b = vec4(data.box.x + data.blur, data.box.y + data.blur, data.box.z -
								2.0 * data.blur, data.box.w - 2.0 *	data.blur);
						opacity = blur_rounded_rect(b, pos, data.blur);
					} else {
						opacity = antialias(r, data.radius_bottom - 1.0, data.radius_bottom, fw2(r, p));
					}
                    out_color = data.color * opacity;
                    return;
                }
            }
        }
    }
	if (data.blur > 0.0) {
		vec4 b = vec4(data.box.x + data.blur, data.box.y + data.blur, data.box.z - 2.0 * data.blur,
				data.box.w - 2.0 * data.blur);
		float opacity = blur_rounded_rect(b, vec2(gl_FragCoord), data.blur);
		out_color = data.color * opacity;
	} else {
		out_color = data. color;
	}
}
