# usage: create_bin.py data.bin words_to_go_into_bin_file

import sys

with open(sys.argv[1], 'wb') as f:
    ba = bytearray(24+8) # sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)
    ba[0] = 0xe9 # magic word
    ba[12] = 5 # idf firmware chip id
    ba += sys.argv[2].encode()
    f.write(ba)
    if(len(ba) < 1024): # app partition size must be at least 1k
        f.write(bytearray(1024-len(ba)))

# esptool.py --port COMx write_flash 0x200000 data.bin
