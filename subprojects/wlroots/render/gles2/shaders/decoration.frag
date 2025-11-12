precision highp float;

uniform bool border;
uniform bool title_bar;
uniform bool dim;
uniform float border_width;
uniform float border_radius;
uniform float title_bar_height;
uniform float title_bar_border_radius;
uniform bool swap_xy;
uniform bool flip_x;
uniform bool flip_y;
uniform vec4 box;
uniform vec4 border_top;
uniform vec4 border_bottom;
uniform vec4 border_left;
uniform vec4 border_right;
uniform vec4 title_bar_color;
uniform vec4 dim_color;

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
    vec4 color = antialias(d, r, r + border_width, fw2(d, p), c);
	if (dim && color.a <= 0.0) {
		return dim_color;
	}
	return color;
}

void main() {
    vec2 pos = vec2(gl_FragCoord);
    float title_height;
    float r0, r1;
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
    if (title_bar) {
        title_height = title_bar_height;
        r0 = 0.0;
        if (rel.y <= title_bar_height) {
            // Here, account for radius
            if (title_bar_border_radius > 0.0) {
                vec2 p;
                if (rel.x < title_bar_border_radius + 0.5) {
                    if (rel.y < title_bar_border_radius + 0.5) {
                        p = rel - vec2(title_bar_border_radius);
                        float r = length(p);
                        if (r > title_bar_border_radius - 1.0) {
                            gl_FragColor = antialias(r, title_bar_border_radius - 1.0, title_bar_border_radius, fw2(r, p), title_bar_color);
                            return;
                        }
                    }
                } else if (rel.x > width - (title_bar_border_radius + 0.5)) {
                    if (rel.y < title_bar_border_radius + 0.5) {
                        p = rel - vec2(width - title_bar_border_radius, title_bar_border_radius);
                        float r = length(p);
                        if (r > title_bar_border_radius - 1.0) {
                            gl_FragColor = antialias(r, title_bar_border_radius - 1.0, title_bar_border_radius, fw2(r, p), title_bar_color);
                            return;
                        }
                    }
                }
            }
            gl_FragColor = title_bar_color;
            return;
        }
    } else {
        title_height = 0.0;
        r0 = border_radius;
    }
    if (border) {
		height = height - title_height;
		rel.y = rel.y - title_height;
        float bw = border_width + 0.5;
        if (border_radius > 0.0) {
            float r1 = border_radius;
            float radius1 = r1 + border_width;
            float radius0 = r0 + border_width;

            vec2 p;
            if (rel.y < radius0 + 0.5) {
                if (r0 > 0.0) {
                    if (rel.x < radius0 + 0.5) {
                        p = rel - vec2(radius0, radius0);
                        vec4 c;
                        if (-p.x < -p.y) {
                            c = border_top;
                        } else {
                            c = border_left;
                        }
                        gl_FragColor = circumference(p, r0, c);
                        return;
                    } else if (rel.x > width - (radius0 + 0.5)) {
                        p = rel - vec2(width - radius0, radius0);
                        vec4 c;
                        if (p.x < -p.y) {
                            c = border_top;
                        } else {
                            c = border_right;
                        }
                        gl_FragColor = circumference(p, r0, c);
                        return;
                    }
                }
            } else if (rel.y > height - (radius1 + 0.5)) {
                if (rel.x < radius1 + 0.5) {
                    p = rel - vec2(radius1, height - radius1);
                    vec4 c;
                    if (-p.x < p.y) {
                        c = border_bottom;
                    } else {
                        c = border_left;
                    }
                    gl_FragColor = circumference(p, r1, c);
                    return;
                } else if (rel.x > width - (radius1 + 0.5)) {
                    p = rel - vec2(width - radius1, height - radius1);
                    vec4 c;
                    if (p.x < p.y) {
                        c = border_bottom;
                    } else {
                        c = border_right;
                    }
                    gl_FragColor = circumference(p, r1, c);
                    return;
                }
            }
        }

        if (rel.x < bw) {
            gl_FragColor = antialias(rel.x, 0.0, border_width, 0.5, border_left);
            return;
        }
        if (rel.x > width - bw) {
            gl_FragColor = antialias(rel.x, width - border_width, width, 0.5, border_right);
            return;
        }
        if (rel.y < bw) {
            gl_FragColor = antialias(rel.y, 0.0, border_width, 0.5, border_top);
            return;
        }
        if (rel.y > height - bw) {
            gl_FragColor = antialias(rel.y, height - border_width, height, 0.5, border_bottom);
            return;
        }
    }
	if (dim) {
		gl_FragColor = dim_color;
	} else {
	    discard;
	}
}
