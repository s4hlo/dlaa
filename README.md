# DirectX 12 Cube Example

Este projeto cria um ambiente 3D simples em C++ usando DirectX 12.

Funcionalidades:
- Um cubo desenhado em 3D
- Uma câmera móvel usando teclas WASD
- Renderização com DirectX 12 e shaders HLSL

## Como compilar

1. Abra o terminal no diretório do projeto (`c:\Users\magno\dlaa-env`).
2. Crie os arquivos de build:

```powershell
cmake -S . -B build
```

3. Compile o projeto:

```powershell
cmake --build build --config Release
```

4. Execute o binário gerado:

```powershell
build\DirectX12Cube.exe
```

## Nota

Você precisa do Windows 10/11 SDK instalado e do Visual Studio com suporte a desenvolvimento Desktop em C++.
