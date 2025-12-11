/***************************************************************************
*          Lempel, Ziv, Storer, and Szymanski Encoding and Decoding
*
*   File    : lzss.c
*   Purpose : Use lzss coding (Storer and Szymanski's modified lz77) to
*             compress/decompress files.
*   Author  : Michael Dipperstein
*   Date    : November 24, 2003
*
****************************************************************************
*   UPDATES
*
*   Date        Change
*   12/10/03    Changed handling of sliding window to better match standard
*               algorithm description.
*   12/11/03    Remebered to copy encoded characters to the sliding window
*               even when there are no more characters in the input stream.
*
****************************************************************************
*
* LZSS: An ANSI C LZss Encoding/Decoding Routine
* Copyright (C) 2003 by Michael Dipperstein (mdipper@cs.ucsb.edu)
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with this library; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*
***************************************************************************/

/***************************************************************************
*                             INCLUDED FILES
***************************************************************************/
#include <unistd.h>
#include <stdio.h>
/* For setmode */
#include <fcntl.h>
#include <stdlib.h>

#ifdef _WIN32
/* For _O_BINARY */
#include <io.h>
#endif



/***************************************************************************
*                            TYPE DEFINITIONS
***************************************************************************/
/* unpacked encoded offset and length, gets packed into 12 bits and 4 bits*/
typedef struct encoded_string_t
{
    int offset;     /* offset to start of longest match */
    int length;     /* length of longest match */
} encoded_string_t;

typedef enum
{
    ENCODE,
    DECODE
} MODES;

/***************************************************************************
*                                CONSTANTS
***************************************************************************/
#define FALSE   0
#define TRUE    1

#define WINDOW_SIZE     1023   /* size of sliding window (10 bits) */

/* maximum match length not encoded and encoded (6 bits) */
#define MAX_UNCODED     2
#define MAX_CODED       (61 + MAX_UNCODED + 1)
#define MAX_LENGTH      63     /* eb_ecl.exe max for dict 512-1023 */

/***************************************************************************
*                            GLOBAL VARIABLES
***************************************************************************/
/* cyclic buffer sliding window of already read characters */
unsigned char slidingWindow[WINDOW_SIZE];
unsigned char uncodedLookahead[MAX_CODED];

/***************************************************************************
*                               PROTOTYPES
***************************************************************************/
void EncodeLZSS(FILE *inFile, FILE *outFile, int dontPad, int exactPad);   /* encoding routine */
void DecodeLZSS(FILE *inFile, FILE *outFile);   /* decoding routine */

/***************************************************************************
*                                FUNCTIONS
***************************************************************************/

/****************************************************************************
*   Function   : main
*   Description: This is the main function for this program, it validates
*                the command line input and, if valid, it will either
*                encode a file using the LZss algorithm or decode a
*                file encoded with the LZss algorithm.
*   Parameters : argc - number of parameters
*                argv - parameter list
*   Effects    : Encodes/Decodes input file
*   Returned   : EXIT_SUCCESS for success, otherwise EXIT_FAILURE.
****************************************************************************/
int main(int argc, char *argv[])
{
    int opt;
    int dontPad;
    int exactPad;
    FILE *inFile, *outFile;  /* input & output files */
    MODES mode;

    /* initialize data */
    inFile = NULL;
    outFile = NULL;
    mode = ENCODE;
    dontPad = 0;
    exactPad = 0;

    /* parse command line */
    while ((opt = getopt(argc, argv, "cdetnpsi:o:h?")) != -1)
    {
        switch(opt)
        {
            case 'c':       /* compression mode */
                mode = ENCODE;
                break;

            case 'd':       /* decompression mode */
                mode = DECODE;
                break;
            case 'e':       /* exact length decompression padding */
                exactPad = 1;
                break;
            case 'i':       /* input file name */
                if (inFile != NULL)
                {
                    fprintf(stderr, "Multiple input files not allowed.\n");
                    fclose(inFile);

                    if (outFile != NULL)
                    {
                        fclose(outFile);
                    }

                    exit(EXIT_FAILURE);
                }
                else if ((inFile = fopen(optarg, "rb")) == NULL)
                {
                    perror("Opening inFile");

                    if (outFile != NULL)
                    {
                        fclose(outFile);
                    }

                    exit(EXIT_FAILURE);
                }
                break;

            case 'o':       /* output file name */
                if (outFile != NULL)
                {
                    fprintf(stderr, "Multiple output files not allowed.\n");
                    fclose(outFile);

                    if (inFile != NULL)
                    {
                        fclose(inFile);
                    }

                    exit(EXIT_FAILURE);
                }
                else if ((outFile = fopen(optarg, "wb")) == NULL)
                {
                    perror("Opening outFile");

                    if (outFile != NULL)
                    {
                        fclose(inFile);
                    }

                    exit(EXIT_FAILURE);
                }
                break;
            case 'p':
                dontPad = 1;
                break;
            case 's':
		#ifdef _WIN32
                _setmode( _fileno( stdin ), _O_BINARY );
                _setmode( _fileno( stdout ), _O_BINARY );
		#endif
                inFile = stdin;
                outFile = stdout;
                break;
            case 'h':
            case '?':
                printf("Usage: lzss <options>\n\n");
                printf("options:\n");
                printf("  -c : Encode input file to output file.\n");
                printf("  -d : Decode input file to output file.\n");
                printf("  -e : pad compressed data to produce exact length decompressed data\n");
                printf("  -i <filename> : Name of input file.\n");
                printf("  -o <filename> : Name of output file.\n");
                printf("  -s : Use STDIN/STDOUT.\n");
                printf("  -p : Do not pad output data to multiples of 0x10.\n");
                printf("  -h | ?  : Print out command line options.\n\n");
                printf("Default: lzss -c\n");
                return(EXIT_SUCCESS);
        }
    }

    /* validate command line */
    if (inFile == NULL)
    {
        fprintf(stderr, "Input file must be provided\n");
        fprintf(stderr, "Enter \"lzss -?\" for help.\n");

        if (outFile != NULL)
        {
            fclose(outFile);
        }

        exit (EXIT_FAILURE);
    }
    else if (outFile == NULL)
    {
        fprintf(stderr, "Output file must be provided\n");
        fprintf(stderr, "Enter \"lzss -?\" for help.\n");

        if (inFile != NULL)
        {
            fclose(inFile);
        }

        exit (EXIT_FAILURE);
    }

    /* we have valid parameters encode or decode */
    if (mode == ENCODE)
    {
        EncodeLZSS(inFile, outFile, dontPad, exactPad);
    }
    else
    {
        DecodeLZSS(inFile, outFile);
    }

    fclose(inFile);
    fclose(outFile);
    return EXIT_SUCCESS;
}

/****************************************************************************
*   Function   : FindMatch
*   Description: This function will search through the slidingWindow
*                dictionary for the longest sequence matching the MAX_CODED
*                long string stored in uncodedLookahed.
*   Parameters : windowHead - head of sliding window
*                uncodedHead - head of uncoded lookahead buffer
*   Effects    : NONE
*   Returned   : The sliding window index where the match starts and the
*                length of the match.  If there is no match a length of
*                zero will be returned.
****************************************************************************/
encoded_string_t FindMatch(int windowHead, int uncodedHead, int uncodedTail)
{
    encoded_string_t matchData;
    int i, j;
    int search_offset;
    int max_check;

    matchData.length = 2;  /* Must beat 2 to be worth encoding (>= 3) */
    matchData.offset = 0;

    /* Search from offset 3 (minimum match) upward, like eb_ecl.exe */
    /* windowHead points to next write position, so data is behind it */
    for (search_offset = 3; search_offset <= WINDOW_SIZE; search_offset++)
    {
        /* Calculate position in circular buffer */
        int candidate_pos = (windowHead - search_offset + WINDOW_SIZE) % WINDOW_SIZE;

        /* Quick rejection: check first byte */
        if (slidingWindow[candidate_pos] != uncodedLookahead[uncodedHead % MAX_CODED])
        {
            continue;
        }

        /* Quick rejection: check byte at current best length position */
        if (matchData.length < uncodedTail)
        {
            int check_pos = (candidate_pos + matchData.length) % WINDOW_SIZE;
            if (slidingWindow[check_pos] != uncodedLookahead[(uncodedHead + matchData.length) % MAX_CODED])
            {
                continue;
            }
        }

        /* Determine max length to check */
        max_check = uncodedTail;
        if (max_check > MAX_CODED - 1)
            max_check = MAX_CODED - 1;
        if (max_check > search_offset)
            max_check = search_offset;  /* Can't match more than offset distance */

        /* Count matching bytes */
        for (j = 0; j < max_check; j++)
        {
            int src_pos = (candidate_pos + j) % WINDOW_SIZE;
            if (slidingWindow[src_pos] != uncodedLookahead[(uncodedHead + j) % MAX_CODED])
            {
                break;
            }
        }

        /* Update best match if this is better (strict >, not >=) */
        if (j > matchData.length)
        {
            matchData.length = j;
            matchData.offset = search_offset;  /* Store as distance back */

            /* Early termination if we hit max length */
            if (j >= MAX_CODED - 1)
            {
                break;
            }
        }
    }

    /* Return 0 length if no match >= 3 found */
    if (matchData.length <= 2)
    {
        matchData.length = 0;
        matchData.offset = 0;
    }

    return matchData;
}

/****************************************************************************
*   Function   : EncodeLZSS
*   Description: This function will read an input file and write an output
*                file encoded using the eb_ecl.exe LZSS variant.
*                Rewritten to match eb_ecl.exe algorithm exactly:
*                - Reads entire file into memory
*                - Searches directly in input buffer (no ring buffer)
*                - Search from offset 3 upward
*   Parameters : inFile - file to encode
*                outFile - file to write encoded output
*   Effects    : inFile is encoded and written to outFile
*   Returned   : NONE
****************************************************************************/
void EncodeLZSS(FILE *inFile, FILE *outFile, int dontPad, int exactPad)
{
    unsigned char *inputData;
    unsigned char flags, flagPos, encodedData[16];
    int nextEncoded;
    long inputSize, inputPos;
    long remaining;
    long compressedSize;
    int i;

    /* Read entire file into memory (like eb_ecl.exe) */
    fseek(inFile, 0, SEEK_END);
    inputSize = ftell(inFile);
    fseek(inFile, 0, SEEK_SET);

    if (inputSize <= 0)
    {
        return;
    }

    inputData = (unsigned char *)malloc(inputSize);
    if (inputData == NULL)
    {
        fprintf(stderr, "Memory allocation failed\n");
        return;
    }

    if (fread(inputData, 1, inputSize, inFile) != (size_t)inputSize)
    {
        free(inputData);
        return;
    }

    flags = 0;
    flagPos = 0x80;
    nextEncoded = 0;
    inputPos = 0;
    compressedSize = 0;

    /* Process input like eb_ecl.exe does */
    while (inputPos < inputSize)
    {
        int searchLimit;
        int bestLength = 2;  /* Must beat 2 (find >= 3) */
        int bestOffset = 0;
        int searchOffset;

        remaining = inputSize - inputPos;

        /* Determine search limit: min(inputPos, WINDOW_SIZE) */
        searchLimit = (inputPos <= WINDOW_SIZE) ? (int)inputPos : WINDOW_SIZE;

        /* Search for matches from offset 3 upward (eb_ecl.exe style) */
        if (searchLimit >= 3)
        {
            for (searchOffset = 3; searchOffset <= searchLimit; searchOffset++)
            {
                unsigned char *current = inputData + inputPos;
                unsigned char *candidate = current - searchOffset;
                int maxCheck;
                int matchLen;

                /* Quick rejection: check first byte */
                if (*current != *candidate)
                {
                    continue;
                }

                /* Quick rejection: check byte at best length position */
                if (bestLength < remaining && current[bestLength] != candidate[bestLength])
                {
                    continue;
                }

                /* Determine max check length - eb_ecl.exe uses min(remaining, offset) */
                /* then caps result to max_length later */
                maxCheck = (remaining < (long)searchOffset) ? (int)remaining : searchOffset;

                /* Count matching bytes */
                for (matchLen = 0; matchLen < maxCheck; matchLen++)
                {
                    if (current[matchLen] != candidate[matchLen])
                    {
                        break;
                    }
                }

                /* Update best if strictly better */
                if (matchLen > bestLength)
                {
                    bestLength = matchLen;
                    bestOffset = searchOffset;
                }

                /* eb_ecl.exe: when match exceeds max_length, cap and exit */
                if (matchLen > MAX_LENGTH)
                {
                    bestLength = MAX_LENGTH;
                    bestOffset = searchOffset;
                    break;
                }
            }
        }

        /* Cap length to MAX_LENGTH (63 for dict 1023) */
        if (bestLength > MAX_LENGTH)
        {
            bestLength = MAX_LENGTH;
        }

        /* Encode the result */
        if (bestLength >= 3 && bestOffset >= 3)
        {
            /* Match: encode as (length, offset) pair */
            /* eb_ecl.exe format: byte1 = (length << 2) | (offset >> 8) */
            /*                    byte2 = offset & 0xFF                 */
            encodedData[nextEncoded++] = (unsigned char)((bestOffset >> 8) | (bestLength << 2));
            encodedData[nextEncoded++] = (unsigned char)(bestOffset & 0xFF);
            flags |= flagPos;
            inputPos += bestLength;
        }
        else
        {
            /* Literal byte */
            encodedData[nextEncoded++] = inputData[inputPos];
            inputPos++;
        }

        if (flagPos == 0x01)
        {
            /* Write flags and encoded data */
            putc(flags, outFile);
            compressedSize++;

            for (i = 0; i < nextEncoded; i++)
            {
                putc(encodedData[i], outFile);
                compressedSize++;
            }

            flags = 0;
            flagPos = 0x80;
            nextEncoded = 0;
        }
        else
        {
            flagPos >>= 1;
        }
    }

    /* write out any remaining encoded data */
    if (nextEncoded != 0)
    {
        int totalSize = (compressedSize + nextEncoded + 1);
        int remainder = 16 - (totalSize % 16);
        /*
        in exact mode, we need to treat padding bytes as a compression command
        otherwise they will pollute the output length for decompressors which are not length restricted
        */
        if (exactPad && (totalSize % 16 != 0)) {
            while ((compressedSize + nextEncoded + 1) % 16 != 0) {
                if(flagPos == 0x00) {
                    break;
                }
                remainder -= 2;
                encodedData[nextEncoded++] = 0;
                encodedData[nextEncoded++] = 0;
                flags |= flagPos;
                flagPos >>= 1;
            }
        }
        putc(flags, outFile);
	    compressedSize++;
        for (i = 0; i < nextEncoded; i++)
        {
            putc(encodedData[i], outFile);
	        compressedSize++;
        }
    }
    /* 
    We exhausted our input data, and might still have leftover bytes to fill 
    we need to write out padding blocks that resolve to a no-op.
    */
    if (exactPad) {
        int remainder = 16 - (compressedSize % 16);
        int padding_lengths[17] = {0x0, 0x1, 0x12, 0x3, 0x14, 0x5, 0x16, 0x7, 0x18, 0x9, 0x1A, 0xB, 0x1C, 0xD, 0x1E, 0xF, 0x0};
        char padding_block[17] = {0xFF, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
        remainder = padding_lengths[remainder];
        for(i = 0; i < remainder; i++) {
            putc(padding_block[i % 0x11], outFile);
            compressedSize++;
        }
    }
    if (dontPad == 0)
    {
        while ((compressedSize % 0x10) != 0)
        {
            putc(0x00, outFile);
            compressedSize++;
        }
    }
    fprintf(stderr, "compressedSize %lx\n", compressedSize);
    free(inputData);
}

/****************************************************************************
*   Function   : DecodeLZSS
*   Description: This function will read an LZss encoded input file and
*                write an output file.  The encoded file uses a slight
*                modification to the LZss algorithm.  I'm not sure who to
*                credit with the slight modification to LZss, but the
*                modification is to group the coded/not coded flag into
*                bytes.  By grouping the flags, the need to be able to
*                write anything other than a byte may be avoided as longs
*                as strings encode as a whole byte multiple.  This algorithm
*                encodes strings as 16 bits (a 12bit offset + a 4 bit length).
*   Parameters : inFile - file to decode
*                outFile - file to write decoded output
*   Effects    : inFile is decoded and written to outFile
*   Returned   : NONE
****************************************************************************/
void DecodeLZSS(FILE *inFile, FILE *outFile)
{
    int  i, c;
    unsigned char flags, flagsUsed;     /* encoded/not encoded flag */
    int nextChar;                       /* next char in sliding window */
    encoded_string_t code;              /* offset/length code for string */

    /* initialize variables */
    flags = 0;
    flagsUsed = 7;
    nextChar = 0;

    /************************************************************************
    * Fill the sliding window buffer with some known vales.  EncodeLZSS must
    * use the same values.  If common characters are used, there's an
    * increased chance of matching to the earlier strings.
    ************************************************************************/
    for (i = 0; i < WINDOW_SIZE; i++)
    {
        slidingWindow[i] = 0x11;
    }

    while (TRUE)
    {
        flags <<= 1;
        flagsUsed++;

        if (flagsUsed == 8)
        {
            /* shifted out all the flag bits, read a new flag */
            if ((c = getc(inFile)) == EOF)
            {
                break;
            }

            flags = c & 0xFF;
            flagsUsed = 0;
        }

        if ((flags & 0x80) == 0)
        {
            /* uncoded character */
            if ((c = getc(inFile)) == EOF)
            {
                break;
            }

            /* write out byte and put it in sliding window */
            putc(c, outFile);
            slidingWindow[nextChar] = c;
            nextChar = (nextChar + 1) % WINDOW_SIZE;
        }
        else
        {
            /* offset and length */
            if ((code.length = getc(inFile)) == EOF)
            {
                break;
            }

            if ((code.offset = getc(inFile)) == EOF)
            {
                break;
            }


            /* unpack offset and length */
            /* eb_ecl.exe format: offset is distance back from current position */
            code.offset = (code.offset + ((code.length & 0x03) << 8));
            code.length = (code.length >> 2);

            /****************************************************************
            * Write out decoded string to file and lookahead.  It would be
            * nice to write to the sliding window instead of the lookahead,
            * but we could end up overwriting the matching string with the
            * new string if abs(offset - next char) < match length.
            ****************************************************************/
            for (i = 0; i < code.length; i++)
            {
                int src_pos;
                if (nextChar >= code.offset) {
                    src_pos = nextChar - code.offset + i;
                } else {
                    src_pos = WINDOW_SIZE - code.offset + nextChar + i;
                }
                c = slidingWindow[src_pos % WINDOW_SIZE];
                putc(c, outFile);
                uncodedLookahead[i] = c;
            }

            /* write out decoded string to sliding window */
            for (i = 0; i < code.length; i++)
            {
                slidingWindow[(nextChar + i) % WINDOW_SIZE] =
                    uncodedLookahead[i];
            }

            nextChar = (nextChar + code.length) % WINDOW_SIZE;
        }
    }
}
