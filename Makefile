include Makefile.in

CPPFLAGS += -DUSE_STANDARD_IOSTREAM

OBJFILES = $(OBJ_DIR)/tid.o $(OBJ_DIR)/wlpdstm.o $(OBJ_DIR)/transaction.o $(OBJ_DIR)/privatization_tree.o

MUBENCH_OBJFILES = $(OBJ_DIR)/intset-rbtree.o

.PHONY: clean $(OBJ_DIR) $(LIB_DIR) $(INCLUDE_DIR) $(INCLUDE_OUT_FILE)

all: $(INCLUDE_OUT_FILE) $(LIB_DIR)/libwlpdstm.a $(OBJ_DIR)/intset-rbtree

###############
# create dirs #
###############

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(LIB_DIR):
	mkdir -p $(LIB_DIR)

$(INCLUDE_DIR):
	mkdir -p $(INCLUDE_DIR)


#######################
# create include file #
#######################

$(INCLUDE_OUT_FILE): $(INCLUDE_DIR)
	echo "#ifndef STM_H_" > $(INCLUDE_OUT_FILE)
	echo "#define STM_H_" >> $(INCLUDE_OUT_FILE)
	echo >> $(INCLUDE_OUT_FILE)
	echo | awk '{input = "$(LPDSTM_CPPFLAGS)";split(input, defs, " ");for(idx in defs) if(defs[idx] != "-D") print "#define " defs[idx]}' >> $(INCLUDE_OUT_FILE)
	cat $(INCLUDE_IN_FILE) >> $(INCLUDE_OUT_FILE)
	echo >> $(INCLUDE_OUT_FILE)
	echo "#endif" >> $(INCLUDE_OUT_FILE)

##################
# create library #
##################

# create lib
$(LIB_DIR)/libwlpdstm.a: $(LIB_DIR) $(OBJFILES)
	$(AR) cru $@ $(OBJFILES)

# compile
$(OBJ_DIR)/tid.o: $(OBJ_DIR) $(STM_SRC_DIR)/tid.cc $(STM_SRC_DIR)/tid.h
	$(CPP) $(CPPFLAGS) $(STM_SRC_DIR)/tid.cc -c -o $@

$(OBJ_DIR)/wlpdstm.o: $(OBJ_DIR) $(STM_SRC_DIR)/wlpdstm.cc $(STM_SRC_DIR)/wlpdstm.h
	$(CPP) $(CPPFLAGS) $(STM_SRC_DIR)/wlpdstm.cc -c -o $@

$(OBJ_DIR)/transaction.o: $(OBJ_DIR) $(STM_SRC_DIR)/transaction.cc $(STM_SRC_DIR)/transaction.h
	$(CPP) $(CPPFLAGS) $(STM_SRC_DIR)/transaction.cc -c -o $@

$(OBJ_DIR)/privatization_tree.o: $(OBJ_DIR) $(STM_SRC_DIR)/privatization_tree.cc $(STM_SRC_DIR)/privatization_tree.h
	$(CPP) $(CPPFLAGS) $(STM_SRC_DIR)/privatization_tree.cc -c -o $@


##################
# create mubench #
##################

$(OBJ_DIR)/intset-rbtree: $(LIB_DIR)/libwlpdstm.a $(MUBENCH_OBJFILES)
	$(CC) $(MUBENCH_CPPFLAGS) -o $@ $^ $(MUBENCH_LDFLAGS)
	cp $(OBJ_DIR)/intset-rbtree .

$(OBJ_DIR)/intset-rbtree.o: $(OBJ_DIR) $(MUBENCH_SRC_DIR)/intset-rbtree.c
	$(CC) $(MUBENCH_CPPFLAGS) $(MUBENCH_SRC_DIR)/intset-rbtree.c -c -o $@

################
# common tasks #
################

clean:
	rm -rf $(TARGET_DIR)
	rm -rf $(LIB_DIR)
	rm -rf $(INCLUDE_DIR)
	rm -rf intset-rbtree


