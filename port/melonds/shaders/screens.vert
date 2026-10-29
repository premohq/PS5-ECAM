#version 450
// PS5-ECAM: a DS screen, a libretro core's picture, or a bar of the touch cursor, as one quad on VideoOut's picture
// (port/melonds/screens.cpp). Its corners come from the vertex index (two triangles): no vertex buffer.

layout(push_constant) uniform Constants
{
	vec4 rect;  // the quad: left, top, right, bottom, in clip space
	vec2 size;  // the screen's pixels: 256 x 192
	float mode; // 0 sharp, 1 smooth, 2 square pixels; 3 flat white (the cursor)
	float rotation; // the screen's picture turned this many quarter turns clockwise (0 to 3)
	vec2 uvScale;   // the part of the image the screen fills (screens.cpp)
} pc;

layout(location = 0) out vec2 vUv;

const vec2 kCorners[6] = vec2[6](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));

void main()
{
	vec2 corner = kCorners[gl_VertexIndex];
	// the point of the screen at this corner of the quad, the picture turned (screens.h, Unturn)
	int turns = int(pc.rotation + 0.5);
	vUv = turns == 1 ? vec2(corner.y, 1.0 - corner.x) : turns == 2 ? vec2(1.0) - corner : turns == 3 ? vec2(1.0 - corner.y, corner.x) : corner;
	gl_Position = vec4(mix(pc.rect.xy, pc.rect.zw, corner), 0.0, 1.0);
}
