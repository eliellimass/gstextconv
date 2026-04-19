# gstextconv

Ferramenta e biblioteca para ler, escrever e inspecionar texturas **GS2D**
(o contêiner `.ast`/`.gs2d` usado na franquia Farming Simulator na versão portátil.
`gstextconv` é um projeto **de código aberto**, mas o formato GS2D em si
**não é aberto** — veja a seção [Licença](#licença).

- CLI estático: `gstextconv` (Windows x64, Android aarch64).
- Biblioteca Python (pybind11): `gstextconv` (wheels `cp313-win_amd64` e
  `cp313-android_aarch64`).
- [`astcenc`](https://github.com/ARM-software/astc-encoder) embutido — não
  depende de binários externos para codificar/decodificar ASTC.

## Instalação

### CLI

Baixe o binário para a sua plataforma na aba de Releases, ou compile a partir
do código-fonte:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/gstextconv --help
```

Para Windows x64 (MSVC) e Android aarch64 (NDK), os `jobs` do GitHub Actions
em `.github/workflows/build.yml` mostram os comandos exatos.

### Biblioteca Python

```bash
pip install gstextconv                     # quando publicado no PyPI
# ou, a partir do repo:
pip install .
```

Requer Python **3.13**.

## Uso — CLI

```
gstextconv [flags globais] <subcomando> [opções]

flags globais:
  -h, --help     mostra ajuda
  -v, --version  imprime a versão
  -l, --license  imprime a licença completa
  -i, --info     imprime metadados da build
```

Subcomandos: `encoder`, `decoder`, `inspect`. Cada um aceita `-h/--help`.

### `gstextconv encoder`

Converte `.png` / `.jpg` → contêiner `.ast` com ASTC (ou formato
uncompressed suportado pelo GS2D).

```bash
# arquivo único
gstextconv encoder -f textura.png -o textura.ast

# lote: todos os PNGs em uma pasta, sobrescrevendo
gstextconv encoder -d ./in -u ./out -O

# salva ao lado do arquivo original e remove a origem
gstextconv encoder -f textura.png -p -x
```

Flags principais:

| flag | descrição |
| --- | --- |
| `-f/--file`, `-b/--batch`, `-d/--dir`, `-r/--recursive` | entradas |
| `-g/--target-game <fs20\|fs23>` | formato do contêiner a emitir |
| `-b/--block-size <NxM>` | bloco ASTC (4x4 … 12x12) |
| `-q/--quality <fast\|medium\|thorough>` | preset do astcenc |
| `-s/--color-space <srgb\|linear\|alpha>` | espaço de cor |
| `-w/--resize <WxH>` | redimensiona antes do encode |
| `-t/--texture-type <2d\|2darray>` | tipo de textura |
| `-n/--ideal-origin <topLeft\|bottomLeft>` | origem do eixo Y gravada |
| `-o/--output`, `-u/--output-dir`, `-O/--overwrite` | saída |
| `-p/--preserve-file-path` | salva ao lado do arquivo original |
| `-x/--delete-source-file` | apaga o arquivo de origem após sucesso |
| `-v/--verbose` | imprime relatório para cada arquivo (ver abaixo) |

### `gstextconv decoder`

Converte `.ast`/`.gs2d` → `.png` (ou outro formato suportado pela extensão
do `-o`).

```bash
# decodifica uma textura e salva o PNG ao lado
gstextconv decoder -f textura.ast -p

# extrai uma layer específica de um 2D array
gstextconv decoder -f array.ast -L 3 -o camada3.png

# lote recursivo
gstextconv decoder -d ./in -r -u ./png -O
```

Flags principais:

| flag | descrição |
| --- | --- |
| `-c/--channels <swizzle>` | ex.: `rgba`, `r0b1` |
| `-i/--mip-index <n>` | mip específico (default 0) |
| `-L/--layer-index <n>` | layer específica (default 0) |
| `-g/--real-origin` | mantém `bottomLeft` (não faz flip automático) |
| `-p/--preserve-file-path`, `-x/--delete-source-file`, `-v/--verbose` | idem encoder |

### `gstextconv inspect`

Imprime metadados do contêiner em JSON. Por padrão é um JSON "bonitinho"
no stdout; com `-o` vai para arquivo (ou para um diretório em modo lote).

```bash
gstextconv inspect -f textura.ast -a              # imprime tudo
gstextconv inspect -d ./in -r -a -o ./json        # um .json por textura
gstextconv inspect -f textura.ast -c -s -n        # só compressão, tamanho, canais
```

Seletores (combinam entre si; se nenhum for passado, equivale a `-a`):

| flag | campo |
| --- | --- |
| `-m/--num-mipmaps` | quantidade de mipmaps |
| `-l/--num-layers`  | quantidade de layers |
| `-c/--compression` | compressão (`astc_NxM` ou `uncompressed`) |
| `-s/--size`        | `width` e `height` da base |
| `--ideal-origin`   | origem ideal gravada no contêiner |
| `--color-space`    | `srgb` / `linear` / `alpha` |
| `-n/--channels`    | número de canais e `color_format` |
| `-a/--all`         | todos os campos acima |

O JSON sempre inclui `container_version` e `texture_type`.

### Saída `--verbose`

Ao passar `-v/--verbose` em qualquer subcomando, cada arquivo processado é
reportado neste formato:

```
gstextconv:
	index: 1;
	filename: in/textura.ast;
	new file: in/textura.png;
	process duration: 28.085ms;
	process type: decoding;
	status: success;
```

`status` pode ser `success` ou `skipped` (quando o destino já existe e
`-O` não foi passado). `process type` é `encoding`, `decoding` ou
`inspection`.

## Uso — Biblioteca Python

```python
import gstextconv

# decodificar
img = gstextconv.decode(open("textura.ast", "rb").read())
img.width, img.height, img.num_mipmaps, img.num_layers
img.mip(0).layer(0).pixels  # bytes RGBA do mip/layer

# inspecionar
meta = gstextconv.inspect(open("textura.ast", "rb").read())
print(meta.compression, meta.color_space, meta.ideal_origin)

# codificar
png_bytes = open("textura.png", "rb").read()
ast = gstextconv.encode(
    png_bytes,
    target_game="fs23",
    block_size=(6, 6),
    quality="medium",
    color_space="srgb",
)
open("textura.ast", "wb").write(ast)
```

A API exata, nomes dos campos e enums estão em
`python/gstextconv/__init__.py`.

## Formatos suportados

| Contêiner | Versão | Suporte |
| --- | --- | --- |
| GS2D / FS20 | v3, v4 | leitura + escrita |
| GS2D / FS23 | v6     | leitura + escrita |

Compressões ASTC testadas: 4x4, 5x4, 5x5, 6x5, 6x6, 8x5, 8x6, 8x8, 10x5,
10x6, 10x8, 10x10, 12x10, 12x12 (todas via astcenc embutido).

Formatos uncompressed: `r8`, `rg16`, `rgb24`, `bgr24`, `rgba32`, `bgra32`,
`rgba64f`, `rgba128f`.

Varredura de validação (`tools/sweep_decode.py`) passa por 8651/8653
amostras de `samples/fs20/gs2d/` e `samples/fs23/gs2d/` (os 2 restantes
são arquivos `.ast` de 0 bytes).

## Arquitetura

```
cli/main.cpp          # executável gstextconv
include/gstextconv/   # API C++ pública
src/                  # container, codec, astc_bridge, image_io, ...
python/_bindings.cpp  # pybind11 → módulo _gstextconv_native
python/gstextconv/    # fachada Python
third_party/          # astcenc, miniz, stb (vendored)
tools/sweep_decode.py # validação em massa
samples/              # amostras FS20/FS23
```

## Licença

Ver [`LICENSE`](LICENSE).

Resumo, em linguagem simples:

- O **código-fonte** deste projeto é liberado sob uma licença do estilo MIT
  — use, modifique e redistribua à vontade.
- O **formato GS2D / `.ast` / `.gs2d`** é uma obra proprietária de
  terceiros. Este projeto existe para fins de **interoperabilidade** e a
  licença do código-fonte **não concede qualquer direito sobre o formato
  em si, nem sobre os assets/texturas** de jogos que o utilizam.
- Você é o único responsável por garantir que o uso desta ferramenta sobre
  qualquer arquivo cumpre os termos do software de origem, os direitos
  autorais dos assets e a legislação aplicável.
- O projeto não tem afiliação nem é endossado pela Giants Software. Ou qualquer outro detentor de direitos do formato ou dos jogos
  que o utilizam.

## Contribuindo

Pull requests são bem-vindos. Antes de abrir um PR maior, abra uma issue
descrevendo o problema / proposta. Mantenha mudanças focadas e
acompanhadas de amostras de teste quando possível.
