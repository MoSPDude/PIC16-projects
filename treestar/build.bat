del treestar.as
del treestar.cof
del treestar.hex
del treestar.hxl
del treestar.lst
del treestar.p1
del treestar.pre
del treestar.sdb
del treestar.sym
del startup.*
picc -v --summary=psect,mem,class --opt=speed --chip=16F628 treestar.c 
