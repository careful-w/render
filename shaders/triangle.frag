#version 330 core
out vec4 FragColor;

in vec3 ourColor;
in vec2 TexCoord;

// texture sampler
uniform sampler2D texture1;
uniform sampler2D texture2;
uniform float mixValue;
uniform int blendMode;  // 0=mix（线性混合）, 1=add（叠加）, 2=multiply（正片叠底）

void main()
{
    vec4 texColor = texture(texture1, TexCoord);
    vec4 vertexColor = vec4(ourColor, 1.0);

    if (blendMode == 0) {
        // 线性混合：lerp(顶点色, 纹理色, mixValue)
        FragColor = mix(vertexColor, texColor, mixValue);
    } else if (blendMode == 1) {
        // 叠加混合：顶点色 + 纹理色 * 混合因子
        FragColor = vertexColor + texColor * mixValue;
    } else if (blendMode == 2) {
        // 乘法混合（正片叠底）：顶点色 * 纹理色，用 mixValue 控制强度
        FragColor = mix(vertexColor, vertexColor * texColor, mixValue);
    } else {
        // 默认只显示纹理
        FragColor = texColor;
    }
}
