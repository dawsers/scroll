precision highp float;

varying vec2 v_texcoord;
uniform sampler2D tex;
uniform float alpha;
uniform vec4 box;
uniform float radius_top;
uniform float radius_bottom;

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
    if (radius_top > 0.0) {
        vec2 pos = vec2(gl_FragCoord);
        vec2 rel = pos.xy - box.xy;
        float width = box.z;
        if (rel.x < radius_top + 0.5) {
            if (rel.y < radius_top + 0.5) {
                vec2 p = rel - vec2(radius_top);
                float r = length(p);
                if (r > radius_top - 1.0) {
                    float opacity = antialias(r, radius_top - 1.0, radius_top, fw2(r, p));
                    gl_FragColor = vec4(texture2D(tex, v_texcoord).rgb, 1.0) * alpha * opacity;
                    return;
                }
            }
        } else if (rel.x > width - (radius_top + 0.5)) {
            if (rel.y < radius_top + 0.5) {
                vec2 p = rel - vec2(width - radius_top, radius_top);
                float r = length(p);
                if (r > radius_top - 1.0) {
                    float opacity = antialias(r, radius_top - 1.0, radius_top, fw2(r, p));
                    gl_FragColor = vec4(texture2D(tex, v_texcoord).rgb, 1.0) * alpha * opacity;
                    return;
                }
            }
        }
    }
    if (radius_bottom > 0.0) {
        vec2 pos = vec2(gl_FragCoord);
        vec2 rel = pos.xy - box.xy;
        float width = box.z;
        float height = box.w;
        if (rel.x < radius_bottom + 0.5) {
            if (rel.y > height - (radius_bottom + 0.5)) {
                vec2 p = rel - vec2(radius_bottom, height - radius_bottom);
                float r = length(p);
                if (r > radius_bottom - 1.0) {
                    float opacity = antialias(r, radius_bottom - 1.0, radius_bottom, fw2(r, p));
                    gl_FragColor = vec4(texture2D(tex, v_texcoord).rgb, 1.0) * alpha * opacity;
                    return;
                }
            }
        } else if (rel.x > width - (radius_bottom + 0.5)) {
            if (rel.y > height - (radius_bottom + 0.5)) {
                vec2 p = rel - vec2(width - radius_bottom, height - radius_bottom);
                float r = length(p);
                if (r > radius_bottom - 1.0) {
                    float opacity = antialias(r, radius_bottom - 1.0, radius_bottom, fw2(r, p));
                    gl_FragColor = vec4(texture2D(tex, v_texcoord).rgb, 1.0) * alpha * opacity;
                    return;
                }
            }
        }
    }
	gl_FragColor = vec4(texture2D(tex, v_texcoord).rgb, 1.0) * alpha;
}
