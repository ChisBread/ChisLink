.SUFFIXES:

EXAMPLES := \
	00_basic_link \
	01_storage_files \
	02_stream_file \
	03_socket_airlink \
	04_ble_scan \
	05_cart_backup \
	06_file_test \
	07_bomberman \
	08_ble_hid_gamepad

.PHONY: all clean $(EXAMPLES)

all: $(EXAMPLES)

$(EXAMPLES):
	$(MAKE) -C $@

clean:
	for dir in $(EXAMPLES); do $(MAKE) -C $$dir clean; done
