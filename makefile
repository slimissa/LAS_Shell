# Définition du compilateur
CC = gcc

# Options de compilation
# -Wall : active tous les warnings
# -Wextra : active les warnings supplémentaires  
# -g : inclut les informations de debug
CFLAGS = -Wall -Wextra -g

# Options d'édition de liens (bibliothèques)
LDFLAGS = -lreadline

# Nom de l'exécutable final
TARGET = las_shell

# Liste des fichiers objets (.o) à générer
OBJS = main.o Commands.o helper.o input_parser.o history.o \
       operators.o pipes.o redirection.o script.o substitution.o \
       alias.o prompt.o

# Règle par défaut (quand on tape juste "make")
all: $(TARGET)

# Comment créer l'exécutable final
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

# Comment créer un fichier .o à partir d'un .c
%.o: %.c my_own_shell.h
	$(CC) $(CFLAGS) -c $< -o $@

# Nettoyer les fichiers générés
clean:
	rm -f $(OBJS) $(TARGET) .las_shell_history .las_aliases

# Compiler et exécuter
run: $(TARGET)
	./$(TARGET)

# Éviter les conflits avec des fichiers du même nom
.PHONY: all clean run