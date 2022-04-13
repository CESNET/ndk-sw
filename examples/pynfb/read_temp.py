#!/usr/bin/python
import nfb

class TempComp(nfb.NFBComp):
	def __init__(self, dev):
		nfb.NFBComp.__init__(self, dev.getComp("netcope,idcomp", 0))
	def getTemp(self):
		self.write32(0x44, 0)
		reg = self.read16(0x80)
		return (reg * 0.0076900482177734375 - 273.15)
	
#dev = nfb.NFBDevice("/dev/nfb0")
#comp = dev.getComp("netcope,bus,mi", 0)
#print("Read SWID:", hex(comp.read32(4)))

dev = nfb.NFBDevice()
comp = TempComp(dev)
print("Temperature:", comp.getTemp())
