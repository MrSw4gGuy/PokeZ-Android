# PokeZ-Android (ZA Recomp para Android)

Notas do port Android para assistentes de IA (e humanos) trabalhando neste clone. As notas gerais do ZA Recomp
(gerador, build no Windows, ferramentas de depuração, regras) ficam no `CLAUDE.md` da raiz do repositório. O projeto porting é o branch `android-port`
de [ZA Recomp](https://github.com/Hexveil404/PokeZ-ARecomp) (LAS Recomp de Peiikai), publicado em
https://github.com/MrSw4gGuy/PokeZ-Android.

## Visão geral

Port Android (arm64) de Pokémon Legends: Z-A (Switch 1) por recompilação estática: o código do jogo vira C, o SO e a
GPU (NVN) são reimplementados sobre Vulkan. O runtime é em C; o build Android roda no WSL (Ubuntu 22.04) e gera
`ZARecomp-za-debug.apk`.

## Comandos essenciais

```bash
# Runtime Android (rápido; confere erros de compilação)
wsl.exe -d Ubuntu-22.04 -u root -- bash -lc "bash '/mnt/d/Claude/Recomp/Pokemon Legends ZA/PokeZ-ARecomp/tools/linux/android-port/build_android.sh'"

# APK completo (sincroniza clone Windows→WSL e empacota; ~100 s)
wsl.exe -d Ubuntu-22.04 -u root -- bash -lc "bash '/mnt/d/Claude/Recomp/Pokemon Legends ZA/PokeZ-ARecomp/tools/linux/android-port/za_apk.sh'"
# Saída: D:/Claude/Recomp/Pokemon Legends ZA/ZARecomp-za-debug.apk — procurar 'ZA APK OK' no fim.

# Instalar no celular (ADB Wi-Fi; a porta muda a cada sessão)
./platform-tools/adb.exe mdns services          # descobrir a porta
./platform-tools/adb.exe connect 192.168.0.99:<porta>
./platform-tools/adb.exe install -r ZARecomp-za-debug.apk
./platform-tools/adb.exe shell am start -n org.recomp.za/.ZARecompActivity   # NÃO é MainActivity

# Logs e configuração no celular
adb pull /storage/emulated/0/Android/data/org.recomp.za/files/lasrecomp.log .
adb shell "grep -E '^(fps60|fps40|frame_gen|shadow_distance|thermal_profile|za_lod) *=' \
  /storage/emulated/0/Android/data/org.recomp.za/files/larecomp.ini"
```

Teste de GPU: `LARECOMP_VK_PROF=1` e `LARECOMP_GPUPROF_AT=<frame>[,<frame>...]` no `env.txt` do celular; exige
force-stop + relançar (env.txt lido no início). Linhas `[gpuprof]` no log. Tema térmico:
`dumpsys thermalservice` (Status 0 none … 4 critical; SKIN é o sensor da pele).

## Arquitetura do runtime (o que morre onde)

- `runtime/main.c` — boot; `runtime/kernel.c` — threads/fibers do jogo.
- `runtime/hle/` — serviços do Switch refeitos. `framepace.c` é o coração do frame pacing do Z-A:
  `rt_za_frame_cfg()` intercepta o relógio do jogo (patch `tools/za_patches.py`, f_70019e3730): o jogo conta
  vsyncs inteiros por quadro (`max(modo+1, passados)`); modo 60 = cfg 0; modo 45 = jogo em modo 30 (2 passos)
  sobre vsync emulado de 90 Hz (`g_vsync_period_ns`, vi.c) + hold de present a cada 2 vsyncs.
- `runtime/nvn/` — a GPU do Switch. `vulkan/exec.c` grava comandos; `vulkan/device.c` cria swapchain
  (pre-rotation no Android) e chama `pace_frame()`; `pace.c` segura o cadência (`hold_cadence`/`hold_30`);
  `output_scale.c` aplica a escala dinâmica; `shaders.c` tem os built-ins (GLSL → SPIR-V na hora).
- `runtime/platform/android.c` — térmico (`plat_thermal_scale`: perfil com/sem cooler em `settings.thermal_profile`;
  `plat_thermal_throttled()`: 1 enquanto a escala térmica < 100%), ADPF perf hints, frame rate hint
  (`ANativeWindow_setFrameRate`).
- `runtime/ui/menu.mm` — menu F1 (opções de vídeo, sombras, Turbo, térmico). `runtime/settings.{c,h}` — INI
  (`larecomp.ini` no celular) e defaults.
- Patches no jogo: `tools/za_patches.py` (endereços da versão 2.0.2; `runtime/game_addresses/`). Tweaks de gráficos
  do jogo: `runtime/hle/za_tweaks.c` (LOD bias, DOF, FXAA, bloom).

## Convenções

- Comentários longos em inglês, explicando o PORQUÊ (histórico e medidas); logs em inglês via `LOG(...)`.
- Opções de menu bilíngues: `TR("English", "Português")`.
- Env vars `LARECOMP_*` lidas com `ENV()` (hle.h); config do usuário: `larecomp.ini`.
- Nunca commitar sem o usuário pedir; nunca subir arquivos do jogo, chaves, o APK nem o `lasgame.so`.

## Estado do projeto (atualizado em 2026-11-06)

- **Sombras (F1 > Vídeo > Distância das sombras)**: Completa / Reduzida / Só próximas / Só personagens / Sem sombras.
  Reduzida = cascata distante na metade do tamanho real do mapa (já escalado pela resolução de sombras do F1: com 50%,
  256×256), copiada para o layer 1 do mapa do jogo por um passe de render (a Adreno não faz blit no D32 do jogo). O
  mapa de IDs de objeto é R8_UINT: a cópia dele usa shader inteiro (`shadow_far_idu.frag`).
- **Perfil térmico (F1 > Vídeo)**: Com cooler / Sem cooler — muda os degraus e a recuperação de
  `plat_thermal_scale` (com cooler 100/85/70/60, baixa com margem 0,90 e sobe abaixo de 0,70, os valores medidos;
  sem cooler até 50%, baixa com 0,85, sobe abaixo de 0,65 e recupera mais devagar).
- **Perfil Turbo (F1 > Vídeo, botão)**: aplica 60 fps com fallback dinâmico, resolução dinâmica do jogo, sombras
  Reduzidas, LOD Desempenho, FG off — e salva o INI.
- **LOD (F1 > Vídeo > Nível de detalhe)**: Do jogo / Alto (bias -1.0) / Desempenho (bias +1.0, via za_tweaks).
- **Pré-compilação de pipelines**: já existia (shader_prep); log mostra cache de 6,4 MB e 0 falhas.
- **ADPF**: já existia (plat_perf_hint + térmico); não duplicar.
- **Modo 40 fps** (`fps40`, F1 > Taxa de quadros), no lugar do antigo 45 (que precisava de 90 Hz: o S25+ só tem
  120, 80, 60, 48, 40, 30, 24 e 10 Hz). O relógio do jogo (2.0.2 `f_7001b29660`) só avança o mundo em vsyncs
  inteiros de 1/60 s: não existe passo de 25 ms, e 45/40 fps feitos com o modo 30 sobre um vsync mais rápido
  aceleravam o mundo (1,5x / 1,33x). Por isso o modo alterna quadros de 1 e 2 passos (`rt_za_frame_cfg`), e cada um
  fica na tela exatamente o tempo do seu passo (`present_pace` espera o intervalo *antes* do quadro nesse modo). Tela
  a 60 Hz, velocidade certa, 25 ms de GPU por quadro em média; a cadência é 16,7 / 33,3 ms (não perfeitamente regular).
- **Glitch nos olhos** (íris com faixa branca): NÃO é o LOD (persiste com za_lod=0). Aberto: investigar shader/
  textura do olho no backend Vulkan.
- **G-buffer não depende de pixels** (medido em 2026-11-06 na estação, `save-diag`, `LARECOMP_FPS=60`,
  `LARECOMP_THERMAL=0`, `LARECOMP_GPUPROF_AT=2400,2700,3000`; passe fs `97b51b1ac26933a3`, 289 desenhos em todas as
  rodadas):

  | Resolução | LOD | G-buffer |
  |---|---|---|
  | 75% | do jogo | 9,7–11,5 ms; repetido: 8,2–9,4 ms |
  | 50% | do jogo | 10,0–10,9 ms |
  | 75% | Desempenho | 10,6–11,3 ms |
  | 50% | Desempenho | 8,8–10,5 ms |

  Com 56% menos pixels o passe custa o mesmo, então texturas menores, resolução e upscaler não aliviam o maior
  custo da GPU (o viés de mip do F1 já cobre a banda de textura, e o Adreno 830 lê BC1–BC7 nativamente, sem
  descompactar). O LOD Desempenho não muda nem o número de desenhos na estação. O custo é por vértice/desenho:
  ~2,9 milhões de vértices por quadro a ~290 milhões/s, muito abaixo do que o Adreno 830 faz; as sombras já
  mostravam o mesmo (metade da resolução, mesmo custo). Próximo passo: descobrir por que o shader de vértice
  traduzido é lento (despejar o GLSL dos VS do G-buffer: leitura dos atributos e dos buffers de constantes na memória
  do AHB?). Simplificar malhas no runtime (meshoptimizer, só índices novos) fica para depois, se os vértices ainda
  pesarem.
- **Ghidra**: `tools/za/nso2elf.py <exefs/main> <saída.elf>` gera o `main` descomprimido como ELF AArch64 (base
  `0x7000000000`, relocações aplicadas); importar com File › Import File. O ELF contém código do jogo: nunca
  commitar.
- **Throttling**: com Turbo + cena pesada, o telefone entra em status 2/3 mesmo com cooler; o retry do 60 fps agora
  espera a escala térmica recuperar (plat_thermal_throttled). Para cenas pesadas, 30 fps segue sendo o modo seguro.

## Lições de debugging (todas vividas aqui)

1. `dlopen` aceita símbolo indefinido em .so — o crash aparece SÓ ao abrir o app
   (`cannot locate symbol "g_fps45_hold"`). Toda variável `extern` usada por pace.c tem de ser DEFINIDA em
   framepace.c.
2. `GA(GAME_MGR)` antes do jogo montar o manager = ponteiro com lixo não-nulo → SIGSEGV dentro de `hold_30`.
   Qualquer novo acesso à memória do jogo precisa de guard baseado no hook do jogo (`g_za_frame_hook`) ou em
   `g_ga`/`fw` válidos.
3. `LARECOMP_FPS=60` no env.txt FORÇA 60 e DESLIGA o fallback — nunca usar em teste de desempenho do modo dinâmico.
4. Diagnóstico de "resolução baixa": olhar `[thermal] headroom ... render scale X%` + `[scale]` no log — a escala
   térmica multiplica a do DRS; ver também `[post] WxH sgsr -> 1920x1080`.
5. `llvm-addr2line -e <libmain.so merged_native_libs> -f -C <pc>` para backtrace do log de crash; os .o em
   `/root/lasrecomp/build/android/rt/**` dão linha exata (pace.c etc.).
6. O INI do celular é a fonte da verdade do teste (`fps60`, `fps40`, `fps60_lock`, `frame_gen`,
   `shadow_distance`, `za_lod`, `thermal_profile`); conferir antes de qualquer medição.

## Pendências abertas

- 40 fps: testar no celular (sensação, velocidade do mundo, a cadência 16,7 / 33,3 ms com `LARECOMP_FG_TIMING`).
- Glitch nos olhos: shader/textura do olho no backend.
- Na pasta de testes: limpar `files/save` e `env.txt` do celular e restaurar `shadow_resolution = 0.5` no ini
  (o usuário aprovou copiar/apagar saves de teste).
- Do BytArch GTA V (GPL-3.0): usar só IDEIAS, não código (licenças: este projeto MIT, aquele GPL-3.0).
