#version 430 core
in vec2 vUV;
layout(location=0) out vec4 FragColor;
uniform sampler2D uTex;
void main(){
    FragColor = texture(uTex, vUV);
}