#version 450

layout(location = 0) out vec4 out_color;
layout(binding = 0) uniform UBO {
	bool border;
	bool title_bar;
	bool dim;
	float border_width;
	float border_radius;
	float title_bar_height;
	float title_bar_border_radius;
	bool swap_xy;
	bool flip_x;
	bool flip_y;
	float pad1;
	float pad2;
	vec4 box;
	vec4 border_top;
	vec4 border_bottom;
	vec4 border_left;
	vec4 border_right;
	vec4 title_bar_color;
	vec4 dim_color;
	vec4 pad3;
} data;

vec4 antialias(float x, float x0, float x1, float fw, vec4 color) {
    float xmax = max(x1, x + fw);
    float xmin = min(x0, x - fw);
    float len = xmax - xmin;
    float d0 = abs(x + fw - x1);
    float d1 = abs(x - fw - x0);
    float overlap = len - d0 - d1;
    float alpha = smoothstep(0.0, 1.0, overlap);
    return color * alpha;
}

float fw2(float r, vec2 p) {
    vec2 ap = abs(p);
    return 0.5 * r / max(ap.x, ap.y);
}

vec4 circumference(vec2 p, float r, vec4 c) {
    float d = length(p);
    vec4 color = antialias(d, r, r + data.border_width, fw2(d, p), c);
	if (data.dim && color.a <= 0.0) {
		return data.dim_color;
	}
	return color;
}

void main() {
    vec2 pos = vec2(gl_FragCoord);
    float title_height;
    float r0, r1;
    vec2 rel = pos.xy - data.box.xy;
	float width, height;
	if (data.flip_x) {
		rel.x = data.box.z - rel.x;
	}
	if (data.flip_y) {
		rel.y = data.box.w - rel.y;
	}
	if (data.swap_xy) {
		rel = rel.yx;
		width = data.box.w;
		height = data.box.z;
	} else {
		width = data.box.z;
		height = data.box.w;
	}
    if (data.title_bar) {
        title_height = data.title_bar_height;
        r0 = 0.0;
        if (rel.y <= data.title_bar_height) {
            // Here, account for radius
            if (data.title_bar_border_radius > 0.0) {
                vec2 p;
                if (rel.x < data.title_bar_border_radius + 0.5) {
                    if (rel.y < data.title_bar_border_radius + 0.5) {
                        p = rel - vec2(data.title_bar_border_radius);
                        float r = length(p);
                        if (r > data.title_bar_border_radius - 1.0) {
                            out_color = antialias(r, data.title_bar_border_radius - 1.0, data.title_bar_border_radius, fw2(r, p), data.title_bar_color);
                            return;
                        }
                    }
                } else if (rel.x > width - (data.title_bar_border_radius + 0.5)) {
                    if (rel.y < data.title_bar_border_radius + 0.5) {
                        p = rel - vec2(width - data.title_bar_border_radius, data.title_bar_border_radius);
                        float r = length(p);
                        if (r > data.title_bar_border_radius - 1.0) {
                            out_color = antialias(r, data.title_bar_border_radius - 1.0, data.title_bar_border_radius, fw2(r, p), data.title_bar_color);
                            return;
                        }
                    }
                }
            }
            out_color = data.title_bar_color;
            return;
        }
    } else {
        title_height = 0.0;
        r0 = data.border_radius;
    }
    if (data.border) {
		height = height - title_height;
		rel.y = rel.y - title_height;
        float bw = data.border_width + 0.5;
        if (data.border_radius > 0.0) {
            float r1 = data.border_radius;
            float radius1 = r1 + data.border_width;
            float radius0 = r0 + data.border_width;

            vec2 p;
            if (rel.y < radius0 + 0.5) {
                if (r0 > 0.0) {
                    if (rel.x < radius0 + 0.5) {
                        p = rel - vec2(radius0, radius0);
                        vec4 c;
                        if (-p.x < -p.y) {
                            c = data.border_top;
                        } else {
                            c = data.border_left;
                        }
                        out_color = circumference(p, r0, c);
                        return;
                    } else if (rel.x > width - (radius0 + 0.5)) {
                        p = rel - vec2(width - radius0, radius0);
                        vec4 c;
                        if (p.x < -p.y) {
                            c = data.border_top;
                        } else {
                            c = data.border_right;
                        }
                        out_color = circumference(p, r0, c);
                        return;
                    }
                }
            } else if (rel.y > height - (radius1 + 0.5)) {
                if (rel.x < radius1 + 0.5) {
                    p = rel - vec2(radius1, height - radius1);
                    vec4 c;
                    if (-p.x < p.y) {
                        c = data.border_bottom;
                    } else {
                        c = data.border_left;
                    }
                    out_color = circumference(p, r1, c);
                    return;
                } else if (rel.x > width - (radius1 + 0.5)) {
                    p = rel - vec2(width - radius1, height - radius1);
                    vec4 c;
                    if (p.x < p.y) {
                        c = data.border_bottom;
                    } else {
                        c = data.border_right;
                    }
                    out_color = circumference(p, r1, c);
                    return;
                }
            }
        }

        if (rel.x < bw) {
            out_color = antialias(rel.x, 0.0, data.border_width, 0.5, data.border_left);
            return;
        }
        if (rel.x > width - bw) {
            out_color = antialias(rel.x, width - data.border_width, width, 0.5, data.border_right);
            return;
        }
        if (rel.y < bw) {
            out_color = antialias(rel.y, 0.0, data.border_width, 0.5, data.border_top);
            return;
        }
        if (rel.y > height - bw) {
            out_color = antialias(rel.y, height - data.border_width, height, 0.5, data.border_bottom);
            return;
        }
    }
	if (data.dim) {
		out_color = data.dim_color;
	} else {
	    discard;
	}
}
