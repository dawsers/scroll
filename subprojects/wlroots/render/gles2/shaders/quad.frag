precision highp float;

varying vec4 v_color;
varying vec2 v_texcoord;
uniform vec4 color;

void main() {
	gl_FragColor = color;
}
