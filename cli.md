# biblioteca para python com core em C++:
```python
import gstextconv;
gstextconv.encoder(
	file:str="filename.png", # filename.png, filename.jpg, filename.bin (raw rgba)
	files:list|tuple=["filename.png" ...], # pode conter mistura de formatos como png, jpg e raw rgba. E nescessário que todas as imagens possuam as mesmas dimensões. Se textureType for 2d, as texturas são comprimidas individualmente.
	output:str="filename.ast", # se não for informado e retornado o conteúdo da textura
	targetGame:str="fs23", # fs20, fs23 and fs26
	mipmaps:int=0, # 0
	blockSize:str|tuple=(6, 6), # 4x4, 5x4, 5x5, 6x5, 6x6, 8x5, 8x6, 8x8, 10x5, 10x6, 10x8, 10x10, 12x10, 12x12
	quality:str="fast", # fast, medium, thorough
	colorSpace:str="srgb", # srgb, linear, alpha
	colorFormat:str="rgba32", # r8, rg16, rgb24, bgr24, rgba32, bgra32, rgba64f, rgba128f
	resize:tuple|None=None, # (WIDTH, HEIGHT), se for void mantém o tamanho atual.
	idealOrigin:str="topLeft", # topLeft, bottomLeft
	textureType:str="2d", # 2d, 2darray
	roughnessChannel:tuple=('r', 'g', 'b', 'a'),
    normalMapFormat:str="rgb", # rg, rgb
)
gstextconv.decoder(
	file:str="filename.ast",
	output:str="filename.png",
	format:str="png", # png, jpg, astc, raw-rgb
	allMipmaps:bool=false, # prevalece sobre mipIndex
	mipIndex:int=0,
	allLayers:bool=false, # prevalece sobre layerIndex
	layerIndex:int=0,
	channels:list=['r', 'g', 'b', 'a'],
	undoIdealOrigin:bool=true,
	pattern:str="{filename}-{mipIndex}-{layerIndex}.{format}
)
image = gstextconv.loader(file)
image.compression # 6x6
image.numMipmaps # 1
image.numLayers # 1
image.origin # topLeft, bottomLeft
image.size # (1024, 1024)
image.channels # ('r', 'g', 'b', 'a')
image.version # v3, v6
image.colorFormat # r8, rg16, rgb24, bgr24, rgba32, bgra32, rgba64f, rgba128f
image.colorSpace # srgb, linear, alpha
image.rawRGBA # conteúdo da imagem em rgba bruto sem compressão ou formato.
image.type # 2d, 2darray
```
# CLI feito integralmente com C++ com uso da biblioteca interna. Sem usar biblioteca dinamica:
```bash
gstextconv <command> [options]

# global flags (também aceitos em cada subcomando --help):
-h, --help        # ajuda geral ou do subcomando
-v, --version     # imprime a versão da biblioteca
-l, --license     # imprime o texto da licença
-i, --info        # imprime metadados da build e capacidades

# "info", "license" e "version" não são mais subcomandos,
# apenas as flags globais acima.

info:
	build release: ;
	build date: ;
	tool name: gstextconv;
	tool description: texture converter for Giants Engine / Farming Simulator assets;
	developer: SnowBit64;
	source: https://github.com/snowbit64/gstextconv.git;
	license type: MIT;
	astc version: ;
	compression backend: astcenc;
	supported textures versions: v3, v6;
	supported formats input: ast, astc, raw-rgba;
	supported formats output: png, jpg, raw-rgba, astc;
	supported platforms and architectures:
	    windows (x86, x64);
	    linux (x86, x64);
	    android (armv7, aarch64);
	cli support: yes;
	gui support: no;
	multithreading: yes;
	gpu acceleration: no;
	unicode paths: yes;
	batch conversion: yes;
	recursive folders: yes;
	max texture size: 16384x16384;
	alpha channel support: yes;
	mipmap support: yes;
	endianness support: little-endian;
	portable binary: yes;
	dependencies: none;
	static build: yes;
	exit codes:
	    0 success;
	    1 invalid file;
	    2 unsupported format;
	    3 conversion failed;
	website: github.com/snowbit64/gstextconv;
	issues: github.com/snowbit64/gstextconv/issues;

gstextconv encoder
-f, --file
-b, --batch
-d, --dir
-o, --output
-r, --recursive
-a, --raw-rgba
-g, --target-game: fs20 (v3), fs23 (v6), fs26 (provavél v6+).
-m, --mipsmaps: número de mipmaps da imagem.
-b, --block-size: (4x4, 5x4, 5x5, 6x5, 6x6, 8x5, 8x6, 8x8, 10x5, 10x6, 10x8, 10x10, 12x10, 12x12)
-q, --quality: fast, medium, thorough
-s, --color-space: srgb, linear, alpha
-c, --color-format:
	r8 (uncompressed 8 bit, 1 channel), 
	rg16 (uncompressed 8 bit 2 channels),
	rgb24 (uncompressed 8 bit 3 channels),
	bgr24 (uncompressed 8 bit 3 channels),
	rgba32 (uncompressed 8 bit 4 channels),
	bgra32 (uncompressed 8 bit 4 channels),
	rgba64f (uncompressed 16 bit float 4 channels),
	rgba128f (uncompressed 32 bit float 4 channels)
-w, --resize
-O, --overwrite
-p, --preserve-file-path # salva o arquivo na mesma pasta do arquivo original
-x, --delete-source-file # apaga o arquivo de origem após a conversão
-t, --texture-type (2d, 2darray)
-m, --num-mipmaps (max, count)
-n, --ideal-origin (topLeft, bottomLeft)
	origin at the top left corner
	origin at the bottom left corner (content will be flipped)
-v, --verbose # imprime um relatório para cada arquivo processado
-rc, --roughness-channel (r, g, b, a)
-nmp, --normal-map-format (rg, rgb)

gstextconv decoder <opção posicional: input> <opção posicional: output>
-f, --file # arquivo único para decodificação
-d, --dir # pasta onde está os arquivos .ast/.gs2d
-b, --batch # abre vários arquivos 
-r, --recursive # percorre --dir recursivamente 
-m, --all-mips # extrai todos os mipmaps
-i, --mip-index # extrai mipmap específico
-L, --layer-index # extrai layer específica (-x passou a ser --delete-source-file)
-l, --all-layers # extrai todas as layers
-c, --channels # rgba, rgb, rg, rba ...
-o, --output # arquivos de saída
-u, --output-dir # pasta de salvamento usada para batch e dir
-O, --overwrite # sobrescreve arquivos 
-g, --real-origin # retorna ao posição normal da textura
-p, --preserve-file-path # salva o arquivo na mesma pasta do arquivo original
-x, --delete-source-file # apaga o arquivo de origem após a decodificação
-v, --verbose # imprime um relatório para cada arquivo processado
-h, --help # mostra os argumentos deste subcomando

gstextconv inspect
-f, --file          # arquivo único
-b, --batch         # lista de arquivos
-d, --dir           # pastas raiz onde os arquivos estão
-r, --recursive     # percorre --dir recursivamente (ignorado com --file/--batch)
-m, --num-mipmaps   # imprime a quantidade de mipmaps da textura
-l, --num-layers    # imprime a quantidade de layers da textura
-c, --compression   # imprime a compressão utilizada na textura
-s, --size          # imprime as dimensões da imagem
-i, --ideal-origin  # imprime a origem ideal da imagem
    --color-space   # imprime o espaço de cor da textura (apenas long, -s é --size)
-n, --channels      # imprime os canais da textura
-a, --all           # imprime todas as informações (padrão se nenhum seletor for passado)
-o, --output        # single file -> nome do arquivo json de saída
                    # múltiplos arquivos -> pasta onde os arquivos json serão salvos
                    # (cada saída reusa o nome base da textura, trocando a extensão)
-v, --verbose       # imprime um relatório para cada arquivo processado
-h, --help          # mostra os argumentos deste subcomando
```
version:
1.0.0
license:
Copyright (c) 2026 snowbit64
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:
  
  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.
  
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

