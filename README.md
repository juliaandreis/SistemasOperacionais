# SistemasOperacionais

O programa envolve 3 processos principais:

1. Compactador – responsável por ler o arquivo de entrada, processar (compactar) e gravar a saída.
Deve utilizar três threads trabalhando em paralelo:
o uma para leitura do arquivo,
o uma para compactação (processamento dos dados),
o e uma para gravação em disco.
2. Descompactador – responsável por ler o arquivo compactado e reconstruir o texto original.
3. Monitor – que exibe informações gerais da execução, como total de palavras lidas, palavras
compactadas, palavras não encontradas no dicionário, tamanho original e compactado, e percentual
final de compressão.
