import struct, pathlib, math
import sys
base=pathlib.Path(sys.argv[1]); name=sys.argv[2]
efi=(base/'BOOTX64.EFI').read_bytes()
# FAT12 floppy 1.44MB, 512-byte sectors, 1 sector per cluster
fat=bytearray(1474560)
fat[0:3]=b'\xeb\x3c\x90';fat[3:11]=b'MXOS    '
struct.pack_into('<HBHBHHBHHHII',fat,11,512,1,1,2,224,2880,0xf0,9,18,2,0,0)
fat[510:512]=b'\x55\xaa'
fat_start=512;root_start=(1+18)*512;data_start=(1+18+14)*512
clusters=math.ceil(len(efi)/512)
sample=bytes.fromhex('b82a000000c3') # x86-64: mov eax,42; ret
(base/'HELLO.BIN').write_bytes(sample)
bin_files=[('HELLO',sample)]
for source in sorted((base.parent/'bins').glob('*.bin')):
 stem=source.stem.upper()
 if stem=='HELLO':raise SystemExit('HELLO.BIN is reserved for the built-in test')
 if not (1<=len(stem)<=8 and stem.isascii() and stem.isalnum()):
  raise SystemExit(f'BIN name must be 1-8 ASCII letters/digits: {source.name}')
 bin_files.append((stem,source.read_bytes()))
assert len(bin_files)<223
entries={2:0xfff,3:0xfff}
for k in range(clusters): entries[4+k]=(5+k if k<clusters-1 else 0xfff)
next_cluster=4+clusters
bin_clusters=[]
for stem,contents in bin_files:
 amount=max(1,math.ceil(len(contents)/512))
 if next_cluster+amount>=2849:raise SystemExit('EFI boot image is full; remove .bin files')
 for k in range(amount):entries[next_cluster+k]=next_cluster+k+1 if k<amount-1 else 0xfff
 bin_clusters.append(next_cluster)
 next_cluster+=amount
for copy in (0,1):
 p=fat_start+copy*9*512
 fat[p:p+3]=b'\xf0\xff\xff'
 for cl,val in entries.items():
  off=p+(cl*3)//2
  if cl%2: fat[off]=(fat[off]&15)|((val&15)<<4);fat[off+1]=(val>>4)&255
  else: fat[off]=val&255;fat[off+1]=(fat[off+1]&240)|((val>>8)&15)
def entry(name,attr,cluster,size=0):
 x=bytearray(32);x[:11]=name;x[11]=attr;struct.pack_into('<H',x,26,cluster);struct.pack_into('<I',x,28,size);return x
fat[root_start:root_start+32]=entry(b'EFI        ',0x10,2)
def cluster_pos(n):return data_start+(n-2)*512
p=cluster_pos(2);fat[p:p+32]=entry(b'.          ',0x10,2);fat[p+32:p+64]=entry(b'..         ',0x10,0);fat[p+64:p+96]=entry(b'BOOT       ',0x10,3)
p=cluster_pos(3);fat[p:p+32]=entry(b'.          ',0x10,3);fat[p+32:p+64]=entry(b'..         ',0x10,2);fat[p+64:p+96]=entry(b'BOOTX64 EFI',0x20,4,len(efi))
fat[cluster_pos(4):cluster_pos(4)+len(efi)]=efi
for index,((stem,contents),cluster) in enumerate(zip(bin_files,bin_clusters),start=1):
 fat[root_start+index*32:root_start+(index+1)*32]=entry((stem.ljust(8)+'BIN').encode('ascii'),0x20,cluster,len(contents))
 fat[cluster_pos(cluster):cluster_pos(cluster)+len(contents)]=contents
(base/'efiboot.img').write_bytes(fat)
S=2048; image_lba=24; image_sectors=len(fat)//S; total=image_lba+image_sectors
iso=bytearray(total*S)
def both16(n):return struct.pack('<H',n)+struct.pack('>H',n)
def both32(n):return struct.pack('<I',n)+struct.pack('>I',n)
def dr(lba,size,name,flags=0):
 z=bytearray(33+len(name)+(len(name)%2==0));z[0]=len(z);z[2:10]=both32(lba);z[10:18]=both32(size);z[18:25]=bytes([126,11,11,10,0,0,0]);z[25]=flags;z[28:32]=both16(1);z[32]=len(name);z[33:33+len(name)]=name;return z
pvd=memoryview(iso)[16*S:17*S];pvd[0]=1;pvd[1:6]=b'CD001';pvd[6]=1;pvd[8:40]=b'MX LANGUAGE'.ljust(32);pvd[40:72]=name.encode()[:32].ljust(32);pvd[80:88]=both32(total);pvd[120:124]=both16(1);pvd[124:128]=both16(1);pvd[128:132]=both16(S);pvd[132:140]=both32(10);struct.pack_into('<I',pvd,140,20);struct.pack_into('>I',pvd,148,21);pvd[156:190]=dr(22,S,b'\x00',2)
boot=memoryview(iso)[17*S:18*S];boot[0]=0;boot[1:6]=b'CD001';boot[6]=1;boot[7:39]=b'EL TORITO SPECIFICATION'.ljust(32);struct.pack_into('<I',boot,71,23)
term=memoryview(iso)[18*S:19*S];term[0]=255;term[1:6]=b'CD001';term[6]=1
# path table: root directory
pt=bytearray(10);pt[0]=1;struct.pack_into('<I',pt,2,22);struct.pack_into('<H',pt,6,1)
iso[20*S:20*S+10]=pt
struct.pack_into('>I',pt,2,22);struct.pack_into('>H',pt,6,1);iso[21*S:21*S+10]=pt
root=dr(22,S,b'\x00',2)+dr(22,S,b'\x01',2)+dr(image_lba,len(fat),b'EFIBOOT.IMG;1')
iso[22*S:22*S+len(root)]=root
catalog=bytearray(S);catalog[0]=1;catalog[1]=0xef;catalog[4:4+21]=b'MXOS UEFI'.ljust(21);catalog[30:32]=b'\x55\xaa'
checksum=(-sum(struct.unpack('<16H',catalog[:32])))&0xffff;struct.pack_into('<H',catalog,28,checksum)
catalog[32]=0x88;catalog[33]=0;struct.pack_into('<H',catalog,38,len(fat)//512);struct.pack_into('<I',catalog,40,image_lba)
iso[23*S:24*S]=catalog;iso[image_lba*S:]=fat
out=base/(name+'.iso');out.write_bytes(iso)
print('Created',out,'size',len(iso),'EFI bytes',len(efi),'El Torito EFI LBA',image_lba)
