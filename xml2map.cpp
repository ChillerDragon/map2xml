#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <direct.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

extern "C" {
#include <base/system.h>
#include <engine/e_datafile.h>
#include <game/mapitems.hpp>
}

/**
 * Pointer to the tiledatas
 */
struct tiledata {
	int width;
	int height;
	unsigned char *data;
	size_t datalen;
};

struct tiledata tiledataBuf[255] = {{0,}, };

/**
 * how many tiledata is in the buf
 */
int tiledataBufLen = 0;


/**
 * Converter structure for images.
 **/
struct image {
	char *name;
	int external;
	char *edata;
};

/**
 * Buffer for the images.
 */
struct image imageBuf[255] = {{0, }, };

/**
 * Number of images.
 */
int imageBufLen = 0;

/**
 * layer written.
 */
int layer_count = 0;

/*****************************************************************************
 * base64 stuff, borrowed and adapted from bob trower, license (its X11/MIT):
 * 
LICENCE:		Copyright (c) 2001 Bob Trower, Trantor Standard Systems Inc.

				Permission is hereby granted, free of charge, to any person
				obtaining a copy of this software and associated
				documentation files (the "Software"), to deal in the
				Software without restriction, including without limitation
				the rights to use, copy, modify, merge, publish, distribute,
				sublicense, and/or sell copies of the Software, and to
				permit persons to whom the Software is furnished to do so,
				subject to the following conditions:

				The above copyright notice and this permission notice shall
				be included in all copies or substantial portions of the
				Software.

				THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
				KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
				WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
				PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS
				OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
				OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
				OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
				SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

/*
** Translation Table as described in RFC1113
*/
static const char cb64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/*
** Translation Table to decode (created by author)
*/
static const char cd64[]="|$$$}rstuvwxyz{$$$$$$$>?@ABCDEFGHIJKLMNOPQRSTUVW$$$$$$XYZ[\\]^_`abcdefghijklmnopq";

/*
** decodeblock
**
** decode 4 '6-bit' characters into 3 8-bit binary bytes
*/
void decodeblock( unsigned char in[4], unsigned char out[3] )
{   
	out[ 0 ] = (unsigned char ) (in[0] << 2 | in[1] >> 4);
	out[ 1 ] = (unsigned char ) (in[1] << 4 | in[2] >> 2);
	out[ 2 ] = (unsigned char ) (((in[2] << 6) & 0xc0) | in[3]);
}

/*
** decode
**
** decode a base64 encoded stream discarding padding, line breaks and noise
*/
int base64_decode(unsigned char *inbuf, int inlen, unsigned char *outbuf, int outbuflen)
{
	unsigned char in[4], out[3], v;
	int i, len;
	int ipos = 0;
	int opos = 0;

	while(ipos < inlen) {
		for(len = 0, i = 0; i < 4 && ipos < inlen; i++ ) {
			v = 0;
			while(ipos < inlen && v == 0 ) {
				v = (unsigned char) inbuf[ipos++];
				v = (unsigned char) ((v < 43 || v > 122) ? 0 : cd64[ v - 43 ]);
				if( v ) {
					v = (unsigned char) ((v == '$') ? 0 : v - 61);
				}
			}
			if(ipos < inlen) {
				len++;
				if( v ) {
					in[ i ] = (unsigned char) (v - 1);
				}
			}
			else {
				in[i] = 0;
			}
		}
		if( len ) {
			decodeblock( in, out );
			for( i = 0; i < len - 1; i++ ) {
				if (opos >= outbuflen) {
					printf("internal error, outbuffer in base64_decode too small\n");
					exit(-1);
				}
				outbuf[opos++] = out[i];
			}
		}
	}
	return opos + 1; // TODO?
}
/**
 * --- End of base64--
 */

/**
 * Get a property, and complain and fail if missing.
 */
xmlChar * getProp(xmlNodePtr node, const xmlChar *name) {
	xmlChar * asw = xmlGetProp(node, name);
	if (asw == NULL) {
		printf("error: missing required attribute \"%s\" from node <%s>\n", name, node->name);
		exit(-1);
	} 
	return asw;
}

int getIntProp(xmlNodePtr node, const xmlChar *name) {
	xmlChar * value = getProp(node, name);
	char *ptr;
	int asw = strtol((char *)value, &ptr, 10);
	if (ptr != NULL && *ptr == 0) {
		return asw;
	}
	printf("error: attribute \"%s\" of \"%s\" = \"%s\" is not an integer.\n", name, node->name, value);
	exit(-1);
}

/**
 * Return a named child of a node. Fail if not there.
 */
xmlNode* findChild(xmlNodePtr node, const xmlChar *name) {
	for (node = node->children; node; node = node->next) {
		if (node->type != XML_ELEMENT_NODE) {
			continue;
		}
		if (!xmlStrcmp(node->name, name)) {
			// found the images block
			return node;
		}
	}
	if (!node) {
		printf("Error: could not find the mandatory <%s> node!\n", (char*) name);
		exit(-1);
	}
	return node;
}


/**
 * Prints out a little help.
 */
void help(char **argv) {
	fprintf(stderr, "Usage %s: [xml-file] [map-file]\n\n", argv[0]);
}

void loadTileLayer(xmlNode *layer_node, int i) {
	int width = getIntProp(layer_node, BAD_CAST "width");
	int height = getIntProp(layer_node, BAD_CAST "height");
	size_t size = width * height * sizeof(TILE);
	tiledataBuf[i].width  = width;
	tiledataBuf[i].height = height;
	tiledataBuf[i].datalen = size;
	tiledataBuf[i].data = (unsigned char *) mem_alloc(size, 1);
	mem_zero(tiledataBuf[i].data, size);
	
	xmlNode *tile_node;
	TILE *tiles = (TILE *) tiledataBuf[i].data;
	for(tile_node = layer_node->children; tile_node; tile_node = tile_node->next) {
		if (tile_node->type != XML_ELEMENT_NODE) {
			continue;
		}
		if (xmlStrcmp(tile_node->name, BAD_CAST "tile")) {
			printf("Error: unexpected node <%s> in <layer>\n", tile_node->name);
			exit(-1);
		}
		int x = getIntProp(tile_node, BAD_CAST "x");
		int y = getIntProp(tile_node, BAD_CAST "y");
		tiles[x + width * y].index = getIntProp(tile_node, BAD_CAST "index");
		tiles[x + width * y].flags = getIntProp(tile_node, BAD_CAST "flags");
	}
}

void loadTileData(xmlNode *root_element) {
	xmlNode *cur_node = findChild(root_element, BAD_CAST "tiledata");
	for (cur_node = cur_node->children; cur_node; cur_node = cur_node->next) {
		if (cur_node->type != XML_ELEMENT_NODE) {
			continue;
		}
		if (xmlStrcmp(cur_node->name, BAD_CAST "layer")) {
			printf("Error: unexpected node <%s> in <tiledata>\n", cur_node->name);
			exit(-1);
		}
		loadTileLayer(cur_node, tiledataBufLen);
		tiledataBufLen++;
	}
}

void transformVersion(xmlNode *root_element, DATAFILE_OUT *df) {
	int version = getIntProp(root_element, BAD_CAST "version");
	if (version != 2) {
		printf("Error: I cannot handle teemap xml version %d.", version);
		exit(-1);
	}
	int map_version = getIntProp(root_element, BAD_CAST "map-version");
	if (map_version != 1) {
		printf("Error: I cannot handle map version %d.", version);
		exit(-1);
	}
	// save version
	{
		MAPITEM_VERSION item;
		memset(&item, 0, sizeof(item));
		item.version = 1;
		datafile_add_item(df, MAPITEMTYPE_VERSION, 0, sizeof(item), &item);
	}
}

void transformImages(xmlNode *root_element, DATAFILE_OUT *df) {
	xmlNode *cur_node = findChild(root_element, BAD_CAST "images");;

	for (cur_node = cur_node->children; cur_node; cur_node = cur_node->next) {
		if (cur_node->type != XML_ELEMENT_NODE) {
			continue;
		}
		if (xmlStrcmp(cur_node->name, BAD_CAST "image")) {
			printf("Error: unexpected node <%s> in <images>\n", cur_node->name);
			exit(-1);
		}
		xmlChar *name = getProp(cur_node, BAD_CAST "name");

		MAPITEM_IMAGE item;
		memset(&item, 0, sizeof(item));
		item.version = getIntProp(cur_node, BAD_CAST "version");
		item.width = getIntProp(cur_node, BAD_CAST "width");
		item.height = getIntProp(cur_node, BAD_CAST "height");
		item.external = getIntProp(cur_node, BAD_CAST "external");
		item.image_name = datafile_add_data(df, strlen((char *) name) + 1, (char *) name);
		imageBuf[imageBufLen].name = (char *) calloc(strlen((char *) name) + 1, 1);
		strcpy(imageBuf[imageBufLen].name, (char*) name);
		imageBuf[imageBufLen].external = item.external;
		if (item.external) {
			// TODO data
		} else {
			item.image_data = -1;
		}
		datafile_add_item(df, MAPITEMTYPE_IMAGE, imageBufLen, sizeof(item), &item);
		imageBufLen++;
	}
}

void transformQuad(xmlNode *cur_node, QUAD *quad) {
	quad->pos_env = getIntProp(cur_node, BAD_CAST "pos-env");
	quad->pos_env_offset = getIntProp(cur_node, BAD_CAST "pos-env-offset");
	quad->color_env = getIntProp(cur_node, BAD_CAST "color-env");
	quad->color_env_offset = getIntProp(cur_node, BAD_CAST "color-env-offset");

	xmlNode *p_node = findChild(cur_node, BAD_CAST "points")->children;
	while (p_node && p_node->type != XML_ELEMENT_NODE) {
		p_node = p_node->next;
	}
	for(int p = 0; p < 5; p++) {
		if (!p_node) {
			printf("Error: Missing point %d in quad\n", p);
			exit(-1);
		}
		if (xmlStrcmp(p_node->name, BAD_CAST "point")) {
			printf("Error: unexpected node <%s> in <points>\n", p_node->name);
			exit(-1);
		}
		quad->points[p].x = getIntProp(p_node, BAD_CAST "x");
		quad->points[p].y = getIntProp(p_node, BAD_CAST "y");
		do {
			p_node = p_node->next;
		} while (p_node && p_node->type != XML_ELEMENT_NODE);
	}
	
	p_node = findChild(cur_node, BAD_CAST "colors")->children;
	while (p_node && p_node->type != XML_ELEMENT_NODE) {
		p_node = p_node->next;
	}
	for(int c = 0; c < 4; c++) {
		if (!p_node) {
			printf("Error: Missing color %d in quad\n", c);
			exit(-1);
		}
		if (xmlStrcmp(p_node->name, BAD_CAST "color")) {
			printf("Error: unexpected node <%s> in <colors>\n", p_node->name);
			exit(-1);
		}
		quad->colors[c].r = getIntProp(p_node, BAD_CAST "r");
		quad->colors[c].g = getIntProp(p_node, BAD_CAST "g");
		quad->colors[c].b = getIntProp(p_node, BAD_CAST "b");
		quad->colors[c].a = getIntProp(p_node, BAD_CAST "a");
		do {
			p_node = p_node->next;
		} while (p_node && p_node->type != XML_ELEMENT_NODE);
	}
	
	p_node = findChild(cur_node, BAD_CAST "texcoords")->children;
	while (p_node && p_node->type != XML_ELEMENT_NODE) {
		p_node = p_node->next;
	}
	for(int p = 0; p < 4; p++) {
		if (!p_node) {
			printf("Error: Missing texcoord %d in quad\n", p);
			exit(-1);
		}
		if (xmlStrcmp(p_node->name, BAD_CAST "point")) {
			printf("Error: unexpected node <%s> in <points>\n", p_node->name);
			exit(-1);
		}
		quad->texcoords[p].x = getIntProp(p_node, BAD_CAST "x");
		quad->texcoords[p].y = getIntProp(p_node, BAD_CAST "y");
		do {
			p_node = p_node->next;
		} while (p_node && p_node->type != XML_ELEMENT_NODE);
	}
}

void transformGroups(xmlNode *root_element, DATAFILE_OUT *df) {
	xmlNode *cur_node = findChild(root_element, BAD_CAST "groups");;
	xmlNode *layer_node = NULL;

	int g=0;
	for (cur_node = cur_node->children; cur_node; cur_node = cur_node->next) {
		if (cur_node->type != XML_ELEMENT_NODE) {
			continue;
		}
		if (xmlStrcmp(cur_node->name, BAD_CAST "group")) {
			printf("Error: unexpected node <%s> in <groups>\n", cur_node->name);
			exit(-1);
		}
		MAPITEM_GROUP gitem;
		memset(&gitem, 0, sizeof(gitem));
		gitem.version = getIntProp(cur_node, BAD_CAST "version");
		gitem.parallax_x = getIntProp(cur_node, BAD_CAST "parallax_x");
		gitem.parallax_y = getIntProp(cur_node, BAD_CAST "parallax_y");
		gitem.offset_x = getIntProp(cur_node, BAD_CAST "offset_x");
		gitem.offset_y = getIntProp(cur_node, BAD_CAST "offset_y");
		gitem.use_clipping = getIntProp(cur_node, BAD_CAST "use_clipping");
		gitem.clip_x = getIntProp(cur_node, BAD_CAST "clip_x");
		gitem.clip_y = getIntProp(cur_node, BAD_CAST "clip_y");
		gitem.clip_w = getIntProp(cur_node, BAD_CAST "clip_w");
		gitem.clip_h = getIntProp(cur_node, BAD_CAST "clip_h");
		gitem.start_layer = layer_count;
		gitem.num_layers = 0;
		for (layer_node = cur_node->children; layer_node; layer_node = layer_node->next) {
			if (layer_node->type != XML_ELEMENT_NODE) {
				continue;
			}
			if (xmlStrcmp(layer_node->name, BAD_CAST "layer")) {
				printf("Error: unexpected node <%s> in <group>\n", layer_node->name);
				exit(-1);
			}
			xmlChar *type = getProp(layer_node, BAD_CAST "type");
			if (!xmlStrcmp(type, BAD_CAST "tiles")) {
				MAPITEM_LAYER_TILEMAP item;
				memset(&item, 0, sizeof(item));
				item.version = getIntProp(layer_node, BAD_CAST "version");
				item.layer.flags = getIntProp(layer_node, BAD_CAST "layer-flags");
				item.layer.type  = LAYERTYPE_TILES;

				item.color.r = 255; // not in use right now
				item.color.g = 255;
				item.color.b = 255;
				item.color.a = 255;
				item.color_env = -1;
				item.color_env_offset = 0;

				item.width = getIntProp(layer_node, BAD_CAST "width");
				item.height = getIntProp(layer_node, BAD_CAST "height");
				item.flags =  getIntProp(layer_node, BAD_CAST "flags");

				xmlChar *imageName = xmlGetProp(layer_node, BAD_CAST "image");
				if (imageName == NULL) {
					item.image = -1;
				} else {
					int im;
					for (im = 0; im < imageBufLen; im++) {
						if (!strcmp(imageBuf[im].name, (char*) imageName)) {
							item.image = im;
							break;
						}
					}
					if (im >= imageBufLen) {
						printf("Error: Layer refers to unkown image \"%s\"\n", imageName);
						exit(-1);
					}
				}

				int tiledata_id =  getIntProp(layer_node, BAD_CAST "tiledata-id");
				if (tiledata_id < 0 || tiledata_id >= tiledataBufLen) {
					printf("Error: Layer refers to tiledata id: \"%d\"\n", tiledata_id);
					exit(-1);
				}
				if (item.width != tiledataBuf[tiledata_id].width ||
					item.height !=  tiledataBuf[tiledata_id].height) {
					printf("Sorry cannot do yet resizing of layers. Fail.\n");
					exit(-1);
				}
				if (tiledataBuf[tiledata_id].datalen != item.width*item.height*sizeof(TILE)) {
					printf("Error, tiledata len mismatch %d!=%d\n", tiledataBuf[tiledata_id].datalen , item.width*item.height*sizeof(TILE));
					exit(-1);
				}

				item.data = datafile_add_data(df, tiledataBuf[tiledata_id].datalen, tiledataBuf[tiledata_id].data);
				datafile_add_item(df, MAPITEMTYPE_LAYER, layer_count, sizeof(item), &item);

				gitem.num_layers++;
				layer_count++;
			} else if (!xmlStrcmp(type, BAD_CAST "quads")) {
				MAPITEM_LAYER_QUADS item;
				memset(&item, 0, sizeof(item));

				item.version = getIntProp(layer_node, BAD_CAST "version");
				item.layer.flags = getIntProp(layer_node, BAD_CAST "layer-flags");
				item.layer.type  = LAYERTYPE_QUADS;
				xmlChar *imageName = xmlGetProp(layer_node, BAD_CAST "image");
				if (imageName == NULL) {
					item.image = -1;
				} else {
					int im;
					for (im = 0; im < imageBufLen; im++) {
						if (!strcmp(imageBuf[im].name, (char*) imageName)) {
							item.image = im;
							break;
						}
					}
					if (im >= imageBufLen) {
						printf("Error: Layer refers to unkown image \"%s\"\n", imageName);
						exit(-1);
					}
				}
					
				// count the quads
				item.num_quads = 0;
				for (xmlNode *q_node = layer_node->children; q_node; q_node = q_node->next) {
					if (q_node->type != XML_ELEMENT_NODE) {
						continue;
					}
					if (xmlStrcmp(q_node->name, BAD_CAST "quad")) {
						printf("Error: unexpected node <%s> in <layer> of quads\n", q_node->name);
						exit(-1);
					}
					item.num_quads++;
				}
				QUAD *quads = (QUAD *) calloc(item.num_quads, sizeof(QUAD));
				QUAD *qpos = quads;
				for (xmlNode *q_node = layer_node->children; q_node; q_node = q_node->next) {
					if (q_node->type != XML_ELEMENT_NODE) {
						continue;
					}
					transformQuad(q_node, qpos);
					qpos++;
				}

				item.data = datafile_add_data_swapped(df, item.num_quads*sizeof(QUAD), quads);
				datafile_add_item(df, MAPITEMTYPE_LAYER, layer_count, sizeof(item), &item);

				gitem.num_layers++;
				layer_count++;
			} else {
				printf("Error: unknown layer type \"%s\"\n", type);
				exit(-1);
			}
		}
		datafile_add_item(df, MAPITEMTYPE_GROUP, g++, sizeof(gitem), &gitem);
	}
}

void transformEnvelopes(xmlNode *root_element, DATAFILE_OUT *df) {
	xmlNode *cur_node = findChild(root_element, BAD_CAST "envelopes");;

	int expect_id = 0;
	int point_count = 0;
	int e = 0;
	for (cur_node = cur_node->children; cur_node; cur_node = cur_node->next) {
		if (cur_node->type != XML_ELEMENT_NODE) {
			continue;
		}
		if (xmlStrcmp(cur_node->name, BAD_CAST "envelope")) {
			printf("Error: unexpected node <%s> in <images>\n", cur_node->name);
			exit(-1);
		}
		int id = getIntProp(cur_node, BAD_CAST "id");
		if (id != expect_id) {
			printf("Error: got envelope id %d but expected %d\n",id, expect_id);
			exit(-1);
		}
		MAPITEM_ENVELOPE item;
		memset(&item, 0, sizeof(item));
		item.version = getIntProp(cur_node, BAD_CAST "version");
		item.channels = getIntProp(cur_node, BAD_CAST "channels");
		item.start_point = point_count;
		item.num_points = 0;
		for(xmlNode *p_node = cur_node->children; p_node; p_node = p_node->next) {
			if (p_node->type != XML_ELEMENT_NODE) {
				continue;
			}
			if (xmlStrcmp(p_node->name, BAD_CAST "envpoint")) {
				printf("Error: unexpected node <%s> in <points>\n", p_node->name);
				exit(-1);
			}
			item.num_points++;
		}
		item.name = -1;
		datafile_add_item(df, MAPITEMTYPE_ENVELOPE, e++, sizeof(item), &item);
		point_count += item.num_points;
		expect_id++;
	}

	// points are extra go through a 2nd time.
	ENVPOINT *points = (ENVPOINT *)calloc(point_count, sizeof(ENVPOINT));
	point_count = 0;

	cur_node = findChild(root_element, BAD_CAST "envelopes");;
	for (cur_node = cur_node->children; cur_node; cur_node = cur_node->next) {
		if (cur_node->type != XML_ELEMENT_NODE) {
			continue;
		}
		int channels = getIntProp(cur_node, BAD_CAST "channels");
		for(xmlNode *p_node = cur_node->children; p_node; p_node = p_node->next) {
			if (p_node->type != XML_ELEMENT_NODE) {
				continue;
			}
			points[point_count].time = getIntProp(p_node, BAD_CAST "time");
			xmlChar *type = getProp(p_node, BAD_CAST "curvetype");
			if (!xmlStrcmp(type, BAD_CAST "step")) {
				points[point_count].curvetype = CURVETYPE_STEP;
			} else if (!xmlStrcmp(type, BAD_CAST "linear")) {
				points[point_count].curvetype = CURVETYPE_LINEAR;
			} else if (!xmlStrcmp(type, BAD_CAST "slow")) {
				points[point_count].curvetype = CURVETYPE_SLOW;
			} else if (!xmlStrcmp(type, BAD_CAST "fast")) {
				points[point_count].curvetype = CURVETYPE_FAST;
			} else if (!xmlStrcmp(type, BAD_CAST "smooth")) {
				points[point_count].curvetype = CURVETYPE_SMOOTH;
			} else {
				printf("Error: don't know of type \"%s\"\n", type);
				exit(-1);
			}
			for(int c = 0; c < channels; c++) {
				char buf[32];
				sprintf(buf, "v%d", c);
				points[point_count].values[c] = getIntProp(p_node, BAD_CAST buf);
			}

			point_count++;
		}
	}
	datafile_add_item(df, MAPITEMTYPE_ENVPOINTS, 0, point_count * sizeof(ENVPOINT), points);
}

/**
 * The main application.
 */
int main(int argc, char **argv) {
	LIBXML_TEST_VERSION

	if(argc != 3) {
		help(argv);
		return -1;
	}
	
	xmlDoc *doc = NULL;
	xmlNode *root_element = NULL;
	
	char current_dir[1024];
	_getcwd(current_dir, sizeof(current_dir));
	char output_filename[1024];
	str_format(output_filename, sizeof(output_filename), "%s/%s", current_dir, argv[2]);
	DATAFILE_OUT *df = datafile_create(output_filename);
	if(!df) {
		printf("Cannot create output map-file '%s'\n", argv[2]);
		return -1;
	}   

	/*parse the file and get the DOM */
	doc = xmlReadFile(argv[1], NULL, 0);
	if (doc == NULL) {
		printf("error: could not parse file %s\n", argv[1]);
		return -1;
	}

	root_element = xmlDocGetRootElement(doc);

	transformVersion(root_element, df);
	loadTileData(root_element);
	transformImages(root_element, df);
	transformGroups(root_element, df);
	transformEnvelopes(root_element, df);

	datafile_finish(df);

	printf("done!\n");
	return 0;
}
