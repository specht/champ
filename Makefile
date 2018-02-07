champ: champ.c labels.h
	gcc -o champ champ.c -lX11

labels.h: parse.rb plot3d20_Output.txt
	./parse.rb

