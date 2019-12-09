CXX=../../../../../out/Default/bin/clang

INPUTS=$(wildcard *.c)

all: $(INPUTS:.c=.wasm)

%.wasm: %.c Makefile
	$(CXX) -gfull -O0 --target=wasm32 -nostdlib -fdebug-prefix-map=`pwd`=. \
	  -Wl,--no-entry,--allow-undefined,--export=Main -o $@ $(filter %.c,$^)

%.dwo %stripped.wasm: %.wasm Makefile split-dwarf.py
	python split-dwarf.py \
	  --dwo $(patsubst %.wasm,%.dwo,$(filter %.wasm,$^)) \
	  --wasm $(patsubst %.wasm,%.stripped.wasm,$(filter %.wasm,$^)) \
	  --module_url $(patsubst %.wasm,%.stripped.wasm,$(filter %.wasm,$^)) \
	  --symbols_url wasm://./$(patsubst %.wasm,%.dwo,$(filter %.wasm,$^)) \
	  $(filter %.wasm,$^)
