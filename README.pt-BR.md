<div align="center">

<img src="docs/icon/icon_1024.png" alt="ZA Recomp" width="128">

# PokeZ-Android

**Um port para Android do ZA Recomp: *Pokémon Legends: Z-A* (versão do Switch 1) rodando nativo num celular arm64,
feito por recompilação estática.**

[English](README.md) · **Português (Brasil)**

[Reportar um bug](https://github.com/MrSw4gGuy/PokeZ-Android/issues) ·
[ZA Recomp para PC](https://github.com/Hexveil404/PokeZ-ARecomp) ·
[Guia de compilação](tools/linux/android-port/README.md)

</div>

---

> [!WARNING]
> **Em desenvolvimento.** No celular de teste, o jogo vai de um save novo até a introdução e a chegada em Lumiose,
> com bugs visuais pendentes (veja [Estado](#estado)). Foi testado num único celular.

Este branch (`android-port`) pega o [ZA Recomp](https://github.com/Hexveil404/PokeZ-ARecomp) - o código do jogo do
Switch traduzido antecipadamente para C, com o sistema operacional e a GPU (NVN) do Switch reimplementados sobre Vulkan -
e o compila para Android em arm64. O código ARM64 do jogo vira código arm64 nativo de novo (as instruções NEON ficam
como são), então nenhum emulador e nenhuma tradução de instruções rodam enquanto você joga.

É baseado no **[LAS Recomp](https://github.com/peiikai/LASRecomp)**, do Peiikai, e no **ZA Recomp**, do Hexveil404.

> [!IMPORTANT]
> **Você precisa da sua própria cópia do jogo, e compila o APK você mesmo.** Nenhum arquivo, código, recurso ou fonte
> do jogo vem incluído. O APK que você compila contém o código do jogo recompilado a partir do *seu* dump, por isso
> não existe APK pronto para baixar, e você nunca deve compartilhar o seu.

## Conteúdo

- [Estado](#estado)
- [Requisitos](#requisitos)
- [Compilar e instalar](#compilar-e-instalar)
- [Controles](#controles) ([Qualidade de imagem](#qualidade-de-imagem))
- [Saves](#saves)
- [O que muda em relação à versão de PC](#o-que-muda-em-relação-à-versão-de-pc)
- [Problemas](#problemas)
- [Créditos](#créditos)
- [Aviso legal](#aviso-legal)

## Estado

Testado num **Samsung Galaxy S25+** (Snapdragon 8 Elite, Adreno 830, 12 GB de RAM, Android 17, driver Vulkan da
própria Qualcomm) com Pokémon Legends: Z-A **2.0.2**, a partir de um save novo.

| | |
|---|---|
| Tela inicial, verificação dos arquivos do jogo | Funciona |
| Escolha de idioma, criação do save, criação de personagem, nome | Funciona |
| Cenas da introdução, trem, Lumiose | Funciona: sem bugs visuais pesados (fora os olhos, abaixo), só o pop-in do próprio jogo |
| Taxa de quadros | 30 fps estáveis (a taxa do próprio jogo no Switch) nas cenas testadas até agora |
| Textos e caixas de diálogo | Funciona |
| Controle (testado: 8BitDo Pro 3, Bluetooth) | Funciona |
| Controles de toque | Funciona (direcional testado nos menus do jogo; o resto em teste) |
| Salvar | Funciona |
| Memória | Cerca de 3 GB em uso |
| Aquecimento | A resolução baixa sozinha conforme o celular esquenta (folga térmica): 75% da tela, até cerca de 45% quando quente |
| Upscaling | Snapdragon Game Super Resolution (o upscaler da própria Qualcomm) por padrão; AMD FSR 1 e bilinear no menu |
| DLC *Mega Dimension* | Ainda não testado |

**Bugs visuais conhecidos** (em correção):
- Alguns olhos aparecem errados: a íris desenhada em quadradinhos azuis e brancos, um retângulo escuro atrás do olho
  de alguns Pokémon.
- Faixas claras retas e fracas sobre rostos e corpos em algumas cenas de close (vistas numa run).
- A imagem fica menos nítida quando o celular esquenta: a resolução baixa para segurar a temperatura (veja
  [Qualidade de imagem](#qualidade-de-imagem)).

## Requisitos

| | |
|---|---|
| **Celular** | Android 12 ou mais novo, arm64 (64 bits), Vulkan 1.3. Testado: só Snapdragon 8 Elite (Adreno 830) |
| **RAM** | 8 GB no mínimo; testado com 12 GB |
| **Armazenamento** | Cerca de 7 GB livres no celular (os dados do jogo têm 6 GB, o APK cerca de 500 MB) |
| **Jogo** | Pokémon Legends: Z-A **2.0.2** (versão do Switch 1): o jogo base e a atualização, com as `prod.keys` do seu próprio Switch |
| **Para compilar** | Um PC com Windows 10/11 e WSL2 (Ubuntu 22.04), ou qualquer Linux x86-64; cerca de 30 GB livres; um cabo USB e o [platform-tools](https://developer.android.com/tools/releases/platform-tools) (`adb`) |

## Compilar e instalar

A compilação toda roda no seu PC, com os scripts de [`tools/linux/android-port/`](tools/linux/android-port/README.md);
esse guia tem cada passo e opção. Resumindo:

1. Prepare as ferramentas uma vez: `setup_wsl.sh`, `install_ndk.sh`, `install_sdk.sh`, depois as bibliotecas com
   `deps_android.sh all`.
2. Importe o seu jogo: coloque o jogo base e a atualização (`.nsp` / `.xci`) e as `prod.keys` numa pasta e rode
   `import_za.sh <essa pasta>`.
3. Compile o código do jogo e o APK: `build_za.sh`, depois `za_apk.sh`. O código do jogo demora na primeira vez.
4. Ative a depuração USB no celular, depois instale o APK e copie os arquivos do jogo:

   ```bash
   adb install -r ZARecomp-za-debug.apk
   adb push za-extracted/exefs /sdcard/Android/data/org.recomp.za/files/extracted/exefs
   adb push za-extracted/romfs.bin /sdcard/Android/data/org.recomp.za/files/extracted/romfs.bin
   ```

5. Abra o **ZA Recomp** no celular e toque em **Play**.

## Controles

- **Controle**: qualquer controle que o Android reconheça (Bluetooth ou USB). É detectado sozinho.
- **Toque**: botões na tela, desenhados por cima do jogo - A/B/X/Y, direcional, L/R/ZL/ZR, + e −, um analógico
  esquerdo que aparece onde o polegar encosta, e arrastar na metade direita da tela para a câmera. Somem enquanto um
  controle está em uso e voltam no próximo toque.
- **Menu de configurações**: o botão ☰ no alto da tela, ou o gesto de Voltar do Android.
- **Layout**: o do Switch por padrão - o botão da direita é o A (confirmar), o de baixo o B (voltar), no jogo e nos
  menus, com os ícones da Nintendo. **Configurações > Controles > Layout A/B/X/Y > Xbox** coloca o A embaixo.

### Qualidade de imagem

O jogo renderiza abaixo da resolução da tela e é ampliado: 75% da tela no começo, e um degrau a menos cada vez que o
celular esquenta (até cerca de 45%). Em **Configurações > Vídeo**:
- **Upscaler**: *Snapdragon GSR* (o padrão: o da própria Qualcomm, feito para Adreno), *AMD FSR 1* ou *Bilinear* (o
  mais suave).
- **Resolução interna**: *Igual à tela* (100%) para uma imagem mais nítida, ao custo de mais calor (e por isso mais
  degraus depois).
- **Anti-aliasing**: *SMAA* suaviza as bordas serrilhadas, com um custo pequeno.

## Saves

Os saves são os arquivos do próprio jogo, em `Android/data/org.recomp.za/files/save/save_0/` no celular. São os mesmos
arquivos do Switch e da versão de PC, então passam de um para o outro sem conversão (JKSV no Switch).

## O que muda em relação à versão de PC

- O código do jogo é compilado antes, no seu PC, e não pela tela inicial: um app Android não consegue rodar o
  compilador.
- O driver da Qualcomm não consegue compartilhar a memória do jogo com a GPU do jeito que os drivers de PC fazem, então
  parte dela é compartilhada por um buffer de hardware do Android. É isso que faz o 3D, os personagens e os textos
  aparecerem.
- A imagem é girada pelo próprio renderizador (a orientação natural do celular é retrato), e a resolução desce um
  degrau quando o celular esquenta.
- Detalhes técnicos: [tools/linux/android-port/README.md](tools/linux/android-port/README.md#what-is-different-on-android).

## Problemas

- **Log**: `Android/data/org.recomp.za/files/lasrecomp.log` no celular (depois de um travamento, o log da execução que
  travou é `lasrecomp-backup-*.log`). Anexe-o a qualquer relato de bug, com o modelo do celular e a versão do Android.
- **Opções de depuração**: as opções do renderizador (`LARECOMP_*`) vão em `Android/data/org.recomp.za/files/env.txt`,
  uma por linha no formato `NOME=valor`, lidas ao abrir.

## Créditos

- **[Peiikai](https://github.com/peiikai)**: criador do [LAS Recomp](https://github.com/peiikai/LASRecomp), o
  recompilador estático, o runtime, os renderizadores e o tradutor de shaders em que este port se baseia.
- **[Hexveil404](https://github.com/Hexveil404)**: o [ZA Recomp](https://github.com/Hexveil404/PokeZ-ARecomp), o port de
  Pokémon Legends: Z-A, e seus colaboradores.
- **[MrSw4gGuy](https://github.com/MrSw4gGuy)**: o port para Android e os testes.
- **Colaboradores do LAS Recomp**: veja [docs/LASRecomp-README.md](docs/LASRecomp-README.md#credits).
- **[Ryujinx](https://github.com/Ryujinx/Ryujinx)**, usado como referência de comportamento (nenhum código copiado),
  **[envytools](https://github.com/envytools/envytools)** e **[Switchbrew](https://switchbrew.org)** pela documentação
  da GPU e do sistema.
- **Bibliotecas**: [SDL 3](https://libsdl.org), [FFmpeg](https://ffmpeg.org), [Dear ImGui](https://github.com/ocornut/imgui),
  [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator),
  [shaderc](https://github.com/google/shaderc), [LibHac](https://github.com/Thealexbarney/LibHac) e as outras listadas
  em [docs/LASRecomp-README.md](docs/LASRecomp-README.md#license). Fontes incluídas no APK: BIZ UDPGothic (SIL OFL) e
  DejaVu Sans.
- **[Snapdragon Game Super Resolution](https://github.com/SnapdragonGameStudios/snapdragon-gsr)**, da Qualcomm
  Innovation Center (BSD-3-Clause, [third_party/sgsr](third_party/sgsr/LICENSE)): o upscaler padrão.

## Aviso legal

O PokeZ-Android é um projeto de fãs. Não é afiliado, endossado nem patrocinado pela Nintendo, Game Freak, Creatures ou
The Pokémon Company. *Pokémon* e *Pokémon Legends: Z-A* são marcas de seus respectivos donos. Nenhum arquivo, código,
recurso ou fonte do jogo vem incluído; você precisa fornecer sua própria cópia obtida legalmente, e o APK compilado a
partir dela é só para o seu aparelho.

O código é distribuído sob a [Licença MIT](LICENSE) (LAS Recomp © Peiikai). Componentes de terceiros mantêm suas
próprias licenças; veja [docs/LASRecomp-README.md](docs/LASRecomp-README.md#license).
