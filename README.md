This is my first block device driver! I really enjoyed creating it :)
I add a litle functionality

Parametrs:

name= ----- name block devices, if ndevices > 1, add letter a,b,c .. z
size name < 30 symbols
defualt name=def_name

ndevices= ----- number devices < 26
defualt ndevices=1

disk_sizes= ----- disk sizes in the appropriate format separated by commas
defualt disk_sizes=100

format= ----- MB or KB separate by comma
defual format=MB

auto_create= ----- 0 auto create defualt block dev , 1 custom block dev
defualt auto_create = 0

b_dev_major= ----- major number for block device
defualt b_dev_major=0

Example:

insmod block_device.ko format=MB,KB,KB ndevices=3, name=example auto_create=1 
