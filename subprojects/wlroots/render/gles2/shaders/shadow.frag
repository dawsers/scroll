precision highp float;

uniform bool enabled;
uniform float blur;
uniform float radius_top;
uniform float radius_bottom;
uniform bool swap_xy;
uniform bool flip_x;
uniform bool flip_y;
uniform vec4 box;
uniform vec4 color;

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

float blur_rect(vec2 rel, vec2 siz, float sigma) {
  return erf_range(vec2(-rel.x, siz.x - rel.x), sigma) * erf_range(vec2(-rel.y, siz.y - rel.y), sigma);
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
	vec2 rel = p.xy - r.xy;
	vec2 siz = r.zw;
	if (flip_x) {
		rel.x = r.z - rel.x;
	}
	if (flip_y) {
		rel.y = r.w - rel.y;
	}
	if (swap_xy) {
		rel = rel.yx;
		siz = siz.yx;
	}
  float result = blur_rect(rel, siz, sigma);
  result -= blur_corner(rel, vec2(radius_top, radius_top), sigma);
  result -= blur_corner(vec2(siz.x - rel.x, rel.y), vec2(radius_top, radius_top), sigma);
  result -= blur_corner(siz - rel, vec2(radius_bottom, radius_bottom), sigma);
  result -= blur_corner(vec2(rel.x, siz.y - rel.y), vec2(radius_bottom, radius_bottom), sigma);

  return result;
}

void main() {
	if (!enabled) {
		discard;
	}
    vec2 pos = vec2(gl_FragCoord);
    vec2 rel = pos.xy - box.xy;
	float width, height;
	if (flip_x) {
		rel.x = box.z - rel.x;
	}
	if (flip_y) {
		rel.y = box.w - rel.y;
	}
	if (swap_xy) {
		rel = rel.yx;
		width = box.w;
		height = box.z;
	} else {
		width = box.z;
		height = box.w;
	}
    if (radius_top > 0.0) {
        if (rel.x < radius_top + 0.5) {
            if (rel.y < radius_top + 0.5) {
                vec2 p = rel - vec2(radius_top);
                float r = length(p);
                if (r > radius_top - 1.0) {
                    float opacity;
					if (blur > 0.0) {
						vec4 b = vec4(box.x + blur, box.y + blur, box.z - 2.0 * blur, box.w - 2.0 *	blur);
						opacity = blur_rounded_rect(b, pos, blur);
					} else {
						opacity = antialias(r, radius_top - 1.0, radius_top, fw2(r, p));
					}
                    gl_FragColor = color * opacity;
                    return;
                }
            }
        } else if (rel.x > width - (radius_top + 0.5)) {
            if (rel.y < radius_top + 0.5) {
                vec2 p = rel - vec2(width - radius_top, radius_top);
                float r = length(p);
                if (r > radius_top - 1.0) {
                    float opacity;
					if (blur > 0.0) {
						vec4 b = vec4(box.x + blur, box.y + blur, box.z - 2.0 * blur, box.w - 2.0 *	blur);
						opacity = blur_rounded_rect(b, pos, blur);
					} else {
						opacity = antialias(r, radius_top - 1.0, radius_top, fw2(r, p));
					}
                    gl_FragColor = color * opacity;
                    return;
                }
            }
        }
    }
    if (radius_bottom > 0.0) {
        if (rel.x < radius_bottom + 0.5) {
            if (rel.y > height - (radius_bottom + 0.5)) {
                vec2 p = rel - vec2(radius_bottom, height - radius_bottom);
                float r = length(p);
                if (r > radius_bottom - 1.0) {
                    float opacity;
					if (blur > 0.0) {
						vec4 b = vec4(box.x + blur, box.y + blur, box.z - 2.0 * blur, box.w - 2.0 *	blur);
						opacity = blur_rounded_rect(b, pos, blur);
					} else {
						opacity = antialias(r, radius_bottom - 1.0, radius_bottom, fw2(r, p));
					}
                    gl_FragColor = color * opacity;
                    return;
                }
            }
        } else if (rel.x > width - (radius_bottom + 0.5)) {
            if (rel.y > height - (radius_bottom + 0.5)) {
                vec2 p = rel - vec2(width - radius_bottom, height - radius_bottom);
                float r = length(p);
                if (r > radius_bottom - 1.0) {
                    float opacity;
					if (blur > 0.0) {
						vec4 b = vec4(box.x + blur, box.y + blur, box.z - 2.0 * blur, box.w - 2.0 *	blur);
						opacity = blur_rounded_rect(b, pos, blur);
					} else {
						opacity = antialias(r, radius_bottom - 1.0, radius_bottom, fw2(r, p));
					}
                    gl_FragColor = color * opacity;
                    return;
                }
            }
        }
    }
	if (blur > 0.0) {
		vec4 b = vec4(box.x + blur, box.y + blur, box.z - 2.0 * blur, box.w - 2.0 *	blur);
		float opacity = blur_rounded_rect(b, pos, blur);
		gl_FragColor = color * opacity;
	} else {
		gl_FragColor = color;
	}
}
