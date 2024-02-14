# TWO-STEP OTA demo

app ota followed by data ota to fully utilize both ota paritions

## How it works

The standard OTA function requires two app partitions of same size, it halves available flash space.

This exmaple is demonstrates how to make use of both OTA partitions, it would be useful if your app contains large constant data, this is how it works:

Partition PA is currently running the app code, while data is compiled into a bin file stored in the other OTA partition PB.

With [memory mapping](https://github.com/espressif/esp-idf/tree/master/examples/storage/partition_api/partition_mmap), app code in PA is able to access the data in PB.

When an OTA update is performed, data in PB is erased, new app code will be downloaded into PB (app OTA), then ESP32 reboot into PB, and downloads new data onto PA (data OTA), thus the name "two step ota".

If app OTA update fails, data in PB is already gone, the user will have to decide wether to try again, or re-download the old data back onto PB.

### Problems

A few obstacles that prevents reusage of the ota related components:

1. Currently the ESP-IDF source code only allows OTA in app partitions, it will check partition type before download.
1. The user can't choose which partition to download firmware/data onto. IDF automatically choose the other ota partition than the one currently running.
1. data OTA in an app partition might be allowed, but it would mark the partition as next boot partition.   

### Solutions

You can either
1. Modify the IDF source code (see https://github.com/espressif/esp-idf/pull/12462) to disable checking of data image
1. Or, as create_bin.py does, prepend a fake header in the data file (see https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/app_image_format.html#structures) to bypass the checking before download. But when you read the data, you need to skip the fake header. A delibrate failed checksum checking after download prevents data partition be marked as next boot partition.

## How to use

Go to menuconfig, setup wifi ssid and password.

Esp32 will set up a server to show partition information at [esp-ip] and accept input at [esp-ip]/ota?url=[url_of_ota_file]

You need to serve compiled ota app/data file in another server from which esp32 can download.