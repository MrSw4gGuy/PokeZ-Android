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
| Arquivos do jogo | **Lidos direto dos seus próprios `.nsp`** (2026-11-28): na tela inicial você escolhe o jogo base, a atualização, a DLC e as keys, uma etapa de cada vez, de onde eles estiverem no celular. Nada grande é copiado (o app guarda só ~40 MB), e um emulador como o Eden pode continuar usando os mesmos arquivos. Cada etapa só aceita o próprio tipo de arquivo, e as keys são conferidas na hora da escolha |
| Escolha de idioma, criação do save, criação de personagem, nome | Funciona |
| Cenas da introdução, trem, Lumiose | Funciona, com o pop-in do próprio jogo; em Lumiose o jogo ainda pode parar com um erro de GPU (abaixo) |
| Taxa de quadros | Três taxas em Configurações > Vídeo: 30 fps (a do próprio jogo no Switch), 40 fps e 60 fps. **40 fps** (2026-11-28): cada quadro avança o mundo exatamente o tempo que fica na tela - inclusive frações do passo de 1/60 s do jogo - com a tela a 80 Hz, então o movimento é regular e na velocidade certa; andando por Lumiose com um save 100% rodou a 37-40 fps, com 89-99% dos quadros na tela exatamente o seu tempo (os antigos modos de 40 / 45 fps, em passos inteiros de 1/60 s, tremiam; 48 fps do mesmo jeito não segurou 48 na cidade). **60 fps**: com a sobreposição CPU/GPU ligada (2026-11-25) a estação segura 60 e Lumiose roda a cerca de 51 no driver da Qualcomm; o **30 / 60 fps dinâmico** vai para 30 em vez de câmera lenta quando 60 não segura |
| Textos e caixas de diálogo | Funciona |
| Controle (testado: 8BitDo Pro 3, Bluetooth) | Funciona |
| Controles de toque | Funciona (direcional testado nos menus do jogo; o resto em teste) |
| Salvar | Funciona |
| Memória | Cerca de 3 GB em uso |
| Aquecimento | O **perfil térmico** (Configurações > Vídeo) escolhe a taxa junto: Com cooler (60 fps, mantém a resolução escolhida), Sem cooler (40 fps), Economia (30 fps); os dois últimos baixam a resolução perto do limite de aquecimento. Com cooler em Lumiose, a folga térmica (1,0 = o celular começa a reduzir o clock) ficou em cerca de 0,73 a 40 fps; a 60 fps estabilizou em 0,92-0,93 |
| Upscaling | Snapdragon Game Super Resolution (o upscaler da própria Qualcomm) por padrão; AMD FSR 1 e bilinear no menu |
| Uso de CPU | Os eventos da janela são checados a cada 4 ms (2026-11-28), em vez da espera do SDL, que girava cerca de 1.000 vezes por segundo: cerca de 21% menos CPU no mesmo trajeto, menos quadros lentos depois do carregamento |
| DLC | A DLC `0100F43008C45002`, preparada pelo `.nsp` dela (com o `title.keys` que tem a chave dela), aparece como pronta no menu do jogo (2026-11-28); o conteúdo dela ainda não foi jogado |

**Bugs conhecidos** (em correção):
- Lumiose: algum tempo depois de chegar, o jogo pode parar com "device lost" (a GPU desistiu). Alguns desenhos
  indiretos leem os parâmetros quando a memória do jogo ali já tem outros dados. Acontecia de vez em quando no driver
  da Qualcomm e sempre com um driver Turnip personalizado; nos testes mais recentes (2026-11-24) aconteceu uma vez no
  driver da Qualcomm e nenhuma com o Turnip.
- Olhos: corrigidos no driver da Qualcomm desde o ZA Recomp v0.1.22 (a correção da íris precisa do quad control do
  driver). Os drivers Turnip personalizados (v39, PurpleVK T30) não têm quad control, então a íris ainda aparece em
  quadradinhos neles.
- Faixas claras retas e fracas sobre rostos e corpos em algumas cenas de close (vistas numa run).
- A imagem fica menos nítida quando o celular esquenta: a resolução baixa para segurar a temperatura (veja
  [Qualidade de imagem](#qualidade-de-imagem)).

## Requisitos

| | |
|---|---|
| **Celular** | Android 12 ou mais novo, arm64 (64 bits), Vulkan 1.3. Testado: só Snapdragon 8 Elite (Adreno 830) |
| **RAM** | 8 GB no mínimo; testado com 12 GB |
| **Armazenamento** | Os seus `.nsp` no celular (cerca de 6,4 GB, onde você já guarda), mais cerca de 600 MB para o APK e os arquivos do próprio app |
| **Jogo** | Pokémon Legends: Z-A **2.0.2** (versão do Switch 1): o jogo base e a atualização em `.nsp`, com as `prod.keys` do seu próprio Switch (de um firmware pelo menos tão novo quanto o jogo pede), e o `title.keys` para uma DLC copiada sem o ticket dela |
| **Para compilar** | Um PC com Windows 10/11 e WSL2 (Ubuntu 22.04), ou qualquer Linux x86-64; cerca de 30 GB livres; um cabo USB e o [platform-tools](https://developer.android.com/tools/releases/platform-tools) (`adb`) |

## Compilar e instalar

A compilação toda roda no seu PC, com os scripts de [`tools/linux/android-port/`](tools/linux/android-port/README.md);
esse guia tem cada passo e opção. Resumindo:

1. Prepare as ferramentas uma vez: `setup_wsl.sh`, `install_ndk.sh`, `install_sdk.sh`, depois as bibliotecas com
   `deps_android.sh all`.
2. Importe o seu jogo no PC (o código do jogo é compilado a partir dele): coloque o jogo base e a atualização
   (`.nsp` / `.xci`) e as `prod.keys` numa pasta e rode `import_za.sh <essa pasta>`.
3. Compile o código do jogo e o APK: `build_za.sh`, depois `za_apk.sh`. O código do jogo demora na primeira vez.
4. Ative a depuração USB no celular, instale o APK e copie os seus `.nsp` e as keys para qualquer pasta do celular
   (por exemplo, a que o seu emulador usa):

   ```bash
   adb install -r ZARecomp-za-debug.apk
   ```

5. Abra o **ZA Recomp** no celular. Na página de preparação, escolha cada arquivo na linha dele: **1. Jogo base**,
   **2. Atualização**, **3. DLC** (opcional), **4. prod.keys** (com o `title.keys`, se tiver), depois toque em
   **Preparar o jogo** e em **Play**. O jogo é lido desses arquivos onde eles estão: não mova nem apague depois.

   (O jeito antigo continua funcionando: `adb push` da pasta `exefs` importada e do `romfs.bin` para
   `/sdcard/Android/data/org.recomp.za/files/extracted/`.)

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
- **Resolução das sombras**: a *do jogo* (1024 x 1024, o padrão), maior, ou *75%* / *50%*. Abaixo do tamanho do jogo
  as bordas viram degraus e as sombras finas viram fileiras de pontos, quase sem economizar GPU: no celular de teste as
  duas cascatas de sombra do sol levaram cerca de 2 ms por quadro na estação, tanto a 50% quanto no tamanho do jogo.
- **Efeitos do jogo** (Pokémon Z-A 2.0.2, mudam na hora): *Nível de detalhe* Alto mantém os modelos detalhados até
  mais longe (menos pop-in, mais tempo da GPU); *Bloom*, *Profundidade de campo* e o *FXAA* do jogo podem ser
  desligados (o bloom desligado também pula a passada dele). Os pontos no jogo foram encontrados pelos
  [mods do Fl4sh9174](https://github.com/Fl4sh9174/Switch-Emulator-Ultrawide-FPS-Mods).
- **Digitar texto** (o nome do personagem e os campos dos nossos menus) usa o teclado próprio do app na tela, por
  toque ou controle; o teclado do Android não aparece.
- **Anti-aliasing**: *SMAA* suaviza as bordas serrilhadas, com um custo pequeno.

### Drivers da GPU

**Configurações > Vídeo > Driver da GPU** escolhe o driver Vulkan: *Do sistema* (o da Qualcomm, o padrão) ou um que
você adicionou. **Importar driver** aceita um `.zip` no formato de pacote que os emuladores de Android usam (um
`meta.json` e o `.so` do driver; por exemplo, as versões do Turnip do
[AdrenoToolsDrivers do K11MCH1](https://github.com/K11MCH1/AdrenoToolsDrivers)) ou um `.so` avulso. Os drivers ficam em
`Android/data/org.recomp.za/files/drivers/`, onde também dá para colocar um `.so` à mão. Um driver novo vale quando o
app abre de novo (**Reiniciar o app agora**, na tela inicial). Se um driver não consegue iniciar a GPU, a próxima
abertura volta sozinha ao do sistema e o menu avisa. Carregado pelo
[libadrenotools](https://github.com/bylaws/libadrenotools).

O Turnip (o driver aberto do Mesa para Adreno) gasta cerca de metade do tempo de GPU do da Qualcomm no passe mais
pesado daqui, mas o v39 e o PurpleVK T30 não têm quad control (a íris aparece em quadradinhos; o menu avisa) e param
em Lumiose com mais frequência (veja os bugs conhecidos). Um driver roda dentro do app como código dele: use só os de
quem você confia.

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
