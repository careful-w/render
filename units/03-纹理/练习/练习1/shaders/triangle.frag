#version 330 core
out vec4 FragColor;

in vec3 ourColor;
in vec2 TexCoord;

// texture sampler
uniform sampler2D texture1;
uniform sampler2D texture2;
uniform float mixValue;
void main()
{
	vec4 texture2Color = texture(texture1, TexCoord);
	FragColor = mix(vec4(ourColor, 1.0), texture2Color, mixValue);
}