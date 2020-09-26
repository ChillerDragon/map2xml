#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <libxml/encoding.h>
#include <libxml/xmlwriter.h>

extern "C" {
#include <base/system.h>
#include <engine/e_datafile.h>
#include <game/mapitems.hpp>
}

#define MY_ENCODING "ISO-8859-1"

/**
 * Pointer to the tiledatas
 */
struct tiledata {
	int width;
	int height;
	unsigned char *data;	
};

struct tiledata tiledataBuf[255] = {{0,}, };

/**
 * how many tiledata is in the buf
 */
int tiledataBufNum = 0;

/**
 * Converter structure for images.
 */
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
int imageBufNum = 0;

/*****************************************************************************
 * base64 stuff, borrowed and adapted from bob trower, license (its X11/MIT):
 * 
LICENCE:        Copyright (c) 2001 Bob Trower, Trantor Standard Systems Inc.

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

/**
 * Returns a guessed value how large the encoded data would be.
 * 
 * It should at least be guaranteed to be larger than what is needed.
 */
int base64_encsize(int size) {
	int asw = size * 4 / 3 + 4;
	asw += asw / 80 * 2 + 5; // for '\n's
	return asw;
}

/*
** encodeblock
**
** encode 3 8-bit binary bytes as 4 '6-bit' characters
*/
void base64_encodeblock( unsigned char in[3], unsigned char out[4], int len )
{
    out[0] = cb64[ in[0] >> 2 ];
    out[1] = cb64[ ((in[0] & 0x03) << 4) | ((in[1] & 0xf0) >> 4) ];
    out[2] = (unsigned char) (len > 1 ? cb64[ ((in[1] & 0x0f) << 2) | ((in[2] & 0xc0) >> 6) ] : '=');
    out[3] = (unsigned char) (len > 2 ? cb64[ in[2] & 0x3f ] : '=');
}

/*
** encode
**
** base64 encode a stream adding padding and line breaks as per spec.
*/
void base64_encode(unsigned char *inbuf, int inlen, unsigned char *outbuf, int outbuflen)
{
    unsigned char in[3], out[4];
    int i, len;
	int ipos = 0, opos = 0;
	int linesize = 80;
	int blocksout = 0;
	outbuf[opos++] = '\n';

    while(ipos < inlen) {
        len = 0;
        for( i = 0; i < 3; i++ ) {
            in[i] = inbuf[ipos++];
			if (ipos < inlen) {
                len++;
            } else {
                in[i] = 0;
            }
        }
        if( len ) {
            base64_encodeblock( in, out, len );
            for( i = 0; i < 4; i++ ) {
				outbuf[opos++] = out[i];
				if (opos >= outbuflen) {
					printf("internal error in base64_encode, output buffer too small\n");
					exit(-1);
				}
            }
			blocksout++;
        }
        if( blocksout >= (linesize/4) || ipos >= inlen ) {
            if( blocksout ) {
				outbuf[opos++] = '\n';	
            }
            blocksout = 0;
        }
    }
	outbuf[opos] = 0;
}

/*
** decodeblock
**
** decode 4 '6-bit' characters into 3 8-bit binary bytes
*/
/*void decodeblock( unsigned char in[4], unsigned char out[3] )
{   
    out[ 0 ] = (unsigned char ) (in[0] << 2 | in[1] >> 4);
    out[ 1 ] = (unsigned char ) (in[1] << 4 | in[2] >> 2);
    out[ 2 ] = (unsigned char ) (((in[2] << 6) & 0xc0) | in[3]);
}*/

/*
** decode
**
** decode a base64 encoded stream discarding padding, line breaks and noise
*/
/*void decode( FILE *infile, FILE *outfile )
{
    unsigned char in[4], out[3], v;
    int i, len;

    while( !feof( infile ) ) {
        for( len = 0, i = 0; i < 4 && !feof( infile ); i++ ) {
            v = 0;
            while( !feof( infile ) && v == 0 ) {
                v = (unsigned char) getc( infile );
                v = (unsigned char) ((v < 43 || v > 122) ? 0 : cd64[ v - 43 ]);
                if( v ) {
                    v = (unsigned char) ((v == '$') ? 0 : v - 61);
                }
            }
            if( !feof( infile ) ) {
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
                putc( out[i], outfile );
            }
        }
    }
}
*/
/**
 * --- End of base64--
 */

/**
 * Prints out a little help.
 */
void help(char **argv) {
	fprintf(stderr, "Usage %s: [map-file] [xml-file]\n\n", argv[0]);
}

void writeIntAttribute(xmlTextWriterPtr wr, xmlChar *name, int a) {
	xmlChar buf[255];
	xmlStrPrintf(buf, sizeof(buf), BAD_CAST "%d", a);
	xmlTextWriterWriteAttribute(wr, name, buf);
}

void transformImages(DATAFILE *df, xmlTextWriterPtr wr) {
	xmlTextWriterStartElement(wr, BAD_CAST "images");
	int start, num;
	datafile_get_type(df, MAPITEMTYPE_IMAGE, &start, &num);

	for(int i = 0; i < num; i++) {
		xmlTextWriterStartElement(wr, BAD_CAST "image");
		MAPITEM_IMAGE *item = (MAPITEM_IMAGE *)datafile_get_item(df, start+i, 0, 0);
		char *name = (char *)datafile_get_data(df, item->image_name);
    	xmlTextWriterWriteAttribute(wr, BAD_CAST "name", BAD_CAST name);

		imageBuf[imageBufNum].external = item->external;
		imageBuf[imageBufNum].name = (char *) calloc(strlen(name) + 1, 1);
		strcpy(imageBuf[imageBufNum].name, name);

		writeIntAttribute(wr, BAD_CAST "version", item->version);
		writeIntAttribute(wr, BAD_CAST "external", item->external);
		if(item->external) {
			// nothing
		} else {
			//char *data = (char *)datafile_get_data(df, item->data);
			imageBuf[imageBufNum].edata = NULL; // TODO
			writeIntAttribute(wr, BAD_CAST "imagedata-id", imageBufNum); 
		}
		imageBufNum++;
		writeIntAttribute(wr, BAD_CAST "width", item->width);
		writeIntAttribute(wr, BAD_CAST "height", item->height);

		xmlTextWriterEndElement(wr);
	}
	xmlTextWriterEndElement(wr);
}

void transformGroups(DATAFILE *df, xmlTextWriterPtr wr) {
	int layers_start, layers_num;
	xmlTextWriterStartElement(wr, BAD_CAST "groups");
	datafile_get_type(df, MAPITEMTYPE_LAYER, &layers_start, &layers_num);

	int start, num;
	datafile_get_type(df, MAPITEMTYPE_GROUP, &start, &num);
	
	for(int g = 0; g < num; g++) {
		MAPITEM_GROUP *gitem = (MAPITEM_GROUP *)datafile_get_item(df, start+g, 0, 0);
		if(gitem->version < 1 || gitem->version > MAPITEM_GROUP::CURRENT_VERSION) {
			printf("There is some group I dont understand!\n");
			exit(-1);
		}
		xmlTextWriterStartElement(wr, BAD_CAST "group");
		writeIntAttribute(wr, BAD_CAST "version", gitem->version);
		writeIntAttribute(wr, BAD_CAST "parallax_x", gitem->parallax_x);
		writeIntAttribute(wr, BAD_CAST "parallax_y", gitem->parallax_y);
		writeIntAttribute(wr, BAD_CAST "offset_x", gitem->offset_x);
		writeIntAttribute(wr, BAD_CAST "offset_y", gitem->offset_y);

		writeIntAttribute(wr, BAD_CAST "use_clipping", gitem->use_clipping);
   		writeIntAttribute(wr, BAD_CAST "clip_x", gitem->clip_x);
		writeIntAttribute(wr, BAD_CAST "clip_y", gitem->clip_y);
		writeIntAttribute(wr, BAD_CAST "clip_w", gitem->clip_w);
		writeIntAttribute(wr, BAD_CAST "clip_h", gitem->clip_h);

		for(int l = 0; l < gitem->num_layers; l++) {
			MAPITEM_LAYER *layer_item = (MAPITEM_LAYER *)datafile_get_item(df, layers_start+gitem->start_layer+l, 0, 0);
			xmlTextWriterStartElement(wr, BAD_CAST "layer");
            if(!layer_item) {
				xmlTextWriterEndElement(wr);
				continue;
			}
			if(layer_item->type == LAYERTYPE_TILES) {
    			xmlTextWriterWriteAttribute(wr, BAD_CAST "type", BAD_CAST "tiles");
				MAPITEM_LAYER_TILEMAP *tilemap_item = (MAPITEM_LAYER_TILEMAP *)layer_item;
				writeIntAttribute(wr, BAD_CAST "version", tilemap_item->version);
				writeIntAttribute(wr, BAD_CAST "flags",  tilemap_item->flags);
				writeIntAttribute(wr, BAD_CAST "layer-flags",  tilemap_item->layer.flags);
				writeIntAttribute(wr, BAD_CAST "width",  tilemap_item->width);
				writeIntAttribute(wr, BAD_CAST "height", tilemap_item->height);
				if (tilemap_item->image >= 0) {
					if (tilemap_item->image > imageBufNum) {
						printf("Error layer points to non-existing image\n");
						exit(-1);
					}
					xmlTextWriterWriteAttribute(wr, BAD_CAST "image", BAD_CAST imageBuf[tilemap_item->image].name);
				}
				unsigned char *data = (unsigned char *) datafile_get_data(df, tilemap_item->data);
				size_t data_size = tilemap_item->width * tilemap_item->height * sizeof(TILE);
				unsigned char *data_copy = (unsigned char *) mem_alloc(data_size, 1);
				mem_copy(data_copy, data, data_size);

				tiledataBuf[tiledataBufNum].width = tilemap_item->width;
				tiledataBuf[tiledataBufNum].height = tilemap_item->height;
				tiledataBuf[tiledataBufNum].data = data_copy;
				writeIntAttribute(wr, BAD_CAST "tiledata-id", tiledataBufNum); 
				tiledataBufNum++;
			} else if (layer_item->type == LAYERTYPE_QUADS) {
    			xmlTextWriterWriteAttribute(wr, BAD_CAST "type", BAD_CAST "quads");
               	MAPITEM_LAYER_QUADS *quads_item = (MAPITEM_LAYER_QUADS *)layer_item;
				writeIntAttribute(wr, BAD_CAST "version", quads_item->version);
				writeIntAttribute(wr, BAD_CAST "layer-flags",  quads_item->layer.flags);

				if (quads_item->image >= 0) {
					if (quads_item->image > imageBufNum) {
						printf("Error layer points to non-existing image\n");
						exit(-1);
					}
					xmlTextWriterWriteAttribute(wr, BAD_CAST "image", BAD_CAST imageBuf[quads_item->image].name);
				}
               	QUAD *data = (QUAD*)datafile_get_data_swapped(df, quads_item->data);
				for(int q = 0; q < quads_item->num_quads; q++) {
					xmlTextWriterStartElement(wr, BAD_CAST "quad");
					writeIntAttribute(wr, BAD_CAST "pos-env", data[q].pos_env);
					writeIntAttribute(wr, BAD_CAST "pos-env-offset", data[q].pos_env_offset);
					writeIntAttribute(wr, BAD_CAST "color-env", data[q].color_env);
					writeIntAttribute(wr, BAD_CAST "color-env-offset", data[q].color_env_offset);

					xmlTextWriterStartElement(wr, BAD_CAST "points");
					for (int p = 0; p < 5; p++) {
						xmlTextWriterStartElement(wr, BAD_CAST "point");
						writeIntAttribute(wr, BAD_CAST "x",  data[q].points[p].x);
						writeIntAttribute(wr, BAD_CAST "y",  data[q].points[p].y);
						xmlTextWriterEndElement(wr);
					}
					xmlTextWriterEndElement(wr);
					
					xmlTextWriterStartElement(wr, BAD_CAST "colors");
					for (int c = 0; c < 4; c++) {
						xmlTextWriterStartElement(wr, BAD_CAST "color");
						writeIntAttribute(wr, BAD_CAST "r",  data[q].colors[c].r);
						writeIntAttribute(wr, BAD_CAST "g",  data[q].colors[c].g);
						writeIntAttribute(wr, BAD_CAST "b",  data[q].colors[c].b);
						writeIntAttribute(wr, BAD_CAST "a",  data[q].colors[c].a);
						xmlTextWriterEndElement(wr);
					}
					xmlTextWriterEndElement(wr);
					
					xmlTextWriterStartElement(wr, BAD_CAST "texcoords");
					for (int p = 0; p < 4; p++) {
						xmlTextWriterStartElement(wr, BAD_CAST "point");
						writeIntAttribute(wr, BAD_CAST "x",  data[q].texcoords[p].x);
						writeIntAttribute(wr, BAD_CAST "y",  data[q].texcoords[p].y);
						xmlTextWriterEndElement(wr);
					}
					xmlTextWriterEndElement(wr);
					
					xmlTextWriterEndElement(wr);
				}
			} else {
				printf("Error unkown layer type\n");
				exit(-1);
			}


			xmlTextWriterEndElement(wr);
		}

		xmlTextWriterEndElement(wr);
	}

	xmlTextWriterEndElement(wr);
}

void transformTileLayer(DATAFILE *df, xmlTextWriterPtr wr, int i) {
	int w = tiledataBuf[i].width;
	int h = tiledataBuf[i].height;
	TILE *tiles = (TILE *) tiledataBuf[i].data;
	
	xmlTextWriterStartElement(wr, BAD_CAST "layer");
	writeIntAttribute(wr, BAD_CAST "id", i);
	writeIntAttribute(wr, BAD_CAST "width",  w);
	writeIntAttribute(wr, BAD_CAST "height", h);
	
	for(int x = 0; x < w; x++)
		for(int y = 0; y < h; y++) {
			TILE tile = tiles[x + y*w];
			if(tile.index) {
				xmlTextWriterStartElement(wr, BAD_CAST "tile");
				writeIntAttribute(wr, BAD_CAST "x", x);
				writeIntAttribute(wr, BAD_CAST "y", y);
				writeIntAttribute(wr, BAD_CAST "index", tile.index);
				writeIntAttribute(wr, BAD_CAST "flags", tile.flags);
				xmlTextWriterEndElement(wr);
			}
		}
	
	xmlTextWriterEndElement(wr);
}

void transformTileData(DATAFILE *df, xmlTextWriterPtr wr) {
	xmlTextWriterStartElement(wr, BAD_CAST "tiledata");
	
	for(int i = 0; i < tiledataBufNum; i++)
		transformTileLayer(df, wr, i);
	
	xmlTextWriterEndElement(wr);
}

void transformEnvelopes(DATAFILE *df, xmlTextWriterPtr wr) {
	xmlTextWriterStartElement(wr, BAD_CAST "envelopes");
	ENVPOINT *points = NULL;

	int start, num;
	datafile_get_type(df, MAPITEMTYPE_ENVPOINTS, &start, &num);
	if(num) {
		points = (ENVPOINT *)datafile_get_item(df, start, 0, 0);
	}

	datafile_get_type(df, MAPITEMTYPE_ENVELOPE, &start, &num);
	for(int e = 0; e < num; e++) {
		xmlTextWriterStartElement(wr, BAD_CAST "envelope");
		writeIntAttribute(wr, BAD_CAST "id", e);
		MAPITEM_ENVELOPE *item = (MAPITEM_ENVELOPE *)datafile_get_item(df, start+e, 0, 0);
		writeIntAttribute(wr, BAD_CAST "version", item->version);
		writeIntAttribute(wr, BAD_CAST "channels", item->channels);
		for(int p = item->start_point; p < item->start_point + item->num_points; p++) {
			xmlTextWriterStartElement(wr, BAD_CAST "envpoint");
			writeIntAttribute(wr, BAD_CAST "time", points[p].time);
			switch(points[p].curvetype) {
			case CURVETYPE_STEP: 
				xmlTextWriterWriteAttribute(wr, BAD_CAST "curvetype", BAD_CAST "step");
				break;
			case CURVETYPE_LINEAR: 
				xmlTextWriterWriteAttribute(wr, BAD_CAST "curvetype", BAD_CAST "linear");
				break;
			case CURVETYPE_SLOW: 
				xmlTextWriterWriteAttribute(wr, BAD_CAST "curvetype", BAD_CAST "slow");
				break;
			case CURVETYPE_FAST: 
				xmlTextWriterWriteAttribute(wr, BAD_CAST "curvetype", BAD_CAST "fast");
				break;
			case CURVETYPE_SMOOTH: 
				xmlTextWriterWriteAttribute(wr, BAD_CAST "curvetype", BAD_CAST "smooth");
				break;
			default:
				printf("error: unknown curve type %d in envelope point %d.\n", points[p].curvetype, p);
				exit(-1);
			}

			for (int c = 0; c < item->channels; c++) {
				char buf[255];
				sprintf(buf, "v%d", c);
				writeIntAttribute(wr, BAD_CAST buf, points[p].values[c]);
			}
			xmlTextWriterEndElement(wr);
		}
		xmlTextWriterEndElement(wr);
	}
	xmlTextWriterEndElement(wr);
}

void transform(DATAFILE *df, xmlTextWriterPtr wr) {
	xmlTextWriterStartElement(wr, BAD_CAST "teemap");

    MAPITEM_VERSION *item = (MAPITEM_VERSION *)datafile_find_item(df, MAPITEMTYPE_VERSION, 0);
    if(!item) {
        printf("cannot handle old maps.\n");
		exit(1);
    }

	writeIntAttribute(wr, BAD_CAST "version", 2); // version of the converter
	writeIntAttribute(wr, BAD_CAST "map-version", item->version); // version of the map itself

	transformEnvelopes(df, wr);
	transformImages(df, wr);
	transformGroups(df, wr);
	transformTileData(df, wr);

	xmlTextWriterEndElement(wr);
}

/**
 * The main application.
 */
int main(int argc, char **argv) {
    LIBXML_TEST_VERSION

	DATAFILE *df;
    xmlTextWriterPtr writer;

	if(argc != 3) {
		help(argv);
		return -1;
	}
	df = datafile_load(argv[1]);
	if (df == NULL) {
		printf("Error: Cannot open input map \"%s\"\n", argv[1]);
		return -1;
	}

    writer = xmlNewTextWriterFilename(argv[2], 0);
	if (writer == NULL) {
		printf("Error: Cannot create outputfile %s.\n", argv[2]);
		return -1;
	}

	xmlTextWriterSetIndent(writer, 4);
	xmlTextWriterStartDocument(writer, NULL, MY_ENCODING, NULL);

	transform(df, writer);

	datafile_unload(df);
    int rc = xmlTextWriterEndDocument(writer);
    if (rc < 0) {
        printf("error at xmlTextWriterEndDocument\n");
        return -1;
    }
	xmlFreeTextWriter(writer);

	printf("done!\n");
	return 0;
}
