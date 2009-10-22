#include "sha.h"
#include <cstring>

// uh..
#ifdef WIN32
    #define LITTLE_ENDIAN
#endif

// Code taken from public domain SHA implementation by Steve Reid

void SHA1::Init()
{
	state[0] = 0x67452301;
	state[1] = 0xEFCDAB89;
	state[2] = 0x98BADCFE;
	state[3] = 0x10325476;
	state[4] = 0xC3D2E1F0;
	total = 0;
}

// Rotate x bits to the left
#ifndef ROL32
#if ((defined _MSC_VER) && (defined NDEBUG))
#define ROL32(_val32, _nBits) _rotl(_val32, _nBits)
#else
#define ROL32(_val32, _nBits) (((_val32)<<(_nBits))|((_val32)>>(32-(_nBits))))
#endif
#endif


/* blk0() and blk() perform the initial expand. */
/* I got the idea of expanding during the round function from SSLeay */
#ifdef LITTLE_ENDIAN
#define blk0(i) (w[i] = (ROL32(block->l[i],24)&0xFF00FF00) |(ROL32(block->l[i],8)&0x00FF00FF))
#else
#define blk0(i) w[i]
#endif
#define blk(i) (w[i&15] = ROL32(w[(i+13)&15]^w[(i+8)&15]^w[(i+2)&15]^w[i&15],1))

/* (R0+R1), R2, R3, R4 are the different operations used in SHA1 */
#define R0(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk0(i)+0x5A827999+ROL32(v,5);w=ROL32(w,30);
#define R1(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk(i)+0x5A827999+ROL32(v,5);w=ROL32(w,30);
#define R2(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0x6ED9EBA1+ROL32(v,5);w=ROL32(w,30);
#define R3(v,w,x,y,z,i) z+=(((w|x)&y)|(w&x))+blk(i)+0x8F1BBCDC+ROL32(v,5);w=ROL32(w,30);
#define R4(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0xCA62C1D6+ROL32(v,5);w=ROL32(w,30);



void SHA1::Process(const byte *data)
{
uint32 a, b, c, d, e, w[16];
typedef union {
    unsigned char c[64];
    uint32 l[16];
} CHAR64LONG16;
CHAR64LONG16* block;
    block = (CHAR64LONG16*)data;
    /* Copy state[] to working vars */
    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
    /* 4 rounds of 20 operations each. Loop unrolled. */
    R0(a,b,c,d,e, 0); R0(e,a,b,c,d, 1); R0(d,e,a,b,c, 2); R0(c,d,e,a,b, 3);
    R0(b,c,d,e,a, 4); R0(a,b,c,d,e, 5); R0(e,a,b,c,d, 6); R0(d,e,a,b,c, 7);
    R0(c,d,e,a,b, 8); R0(b,c,d,e,a, 9); R0(a,b,c,d,e,10); R0(e,a,b,c,d,11);
    R0(d,e,a,b,c,12); R0(c,d,e,a,b,13); R0(b,c,d,e,a,14); R0(a,b,c,d,e,15);
    R1(e,a,b,c,d,16); R1(d,e,a,b,c,17); R1(c,d,e,a,b,18); R1(b,c,d,e,a,19);
    R2(a,b,c,d,e,20); R2(e,a,b,c,d,21); R2(d,e,a,b,c,22); R2(c,d,e,a,b,23);
    R2(b,c,d,e,a,24); R2(a,b,c,d,e,25); R2(e,a,b,c,d,26); R2(d,e,a,b,c,27);
    R2(c,d,e,a,b,28); R2(b,c,d,e,a,29); R2(a,b,c,d,e,30); R2(e,a,b,c,d,31);
    R2(d,e,a,b,c,32); R2(c,d,e,a,b,33); R2(b,c,d,e,a,34); R2(a,b,c,d,e,35);
    R2(e,a,b,c,d,36); R2(d,e,a,b,c,37); R2(c,d,e,a,b,38); R2(b,c,d,e,a,39);
    R3(a,b,c,d,e,40); R3(e,a,b,c,d,41); R3(d,e,a,b,c,42); R3(c,d,e,a,b,43);
    R3(b,c,d,e,a,44); R3(a,b,c,d,e,45); R3(e,a,b,c,d,46); R3(d,e,a,b,c,47);
    R3(c,d,e,a,b,48); R3(b,c,d,e,a,49); R3(a,b,c,d,e,50); R3(e,a,b,c,d,51);
    R3(d,e,a,b,c,52); R3(c,d,e,a,b,53); R3(b,c,d,e,a,54); R3(a,b,c,d,e,55);
    R3(e,a,b,c,d,56); R3(d,e,a,b,c,57); R3(c,d,e,a,b,58); R3(b,c,d,e,a,59);
    R4(a,b,c,d,e,60); R4(e,a,b,c,d,61); R4(d,e,a,b,c,62); R4(c,d,e,a,b,63);
    R4(b,c,d,e,a,64); R4(a,b,c,d,e,65); R4(e,a,b,c,d,66); R4(d,e,a,b,c,67);
    R4(c,d,e,a,b,68); R4(b,c,d,e,a,69); R4(a,b,c,d,e,70); R4(e,a,b,c,d,71);
    R4(d,e,a,b,c,72); R4(c,d,e,a,b,73); R4(b,c,d,e,a,74); R4(a,b,c,d,e,75);
    R4(e,a,b,c,d,76); R4(d,e,a,b,c,77); R4(c,d,e,a,b,78); R4(b,c,d,e,a,79);
    /* Add the working vars back into context.state[] */
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

void SHA1::Update(const byte *data, size_t len )
{
	using std::memcpy;
	size_t j, i;

	if (!len)
		return;

	j = (size_t)total & 0x3F;

	total += len;

	if( j && len >= (i = 64 - j) ) {
		memcpy(buffer + j, data, i );
		Process(buffer);
		len -= i;
		data += i;
		j = 0;
	}

	while (len >= 64) {
		Process(data);
		data += 64;
		len -= 64;
	}

	if (len) {
		memcpy(buffer + j, data, len);
	}
}

static const byte sha1_padding[64] = {
	128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

union bytes64 {
	uint64 i64;
	uint32 i32[2];
    	uint8 i8[8];
};

void SHA1::Finish(byte *digest)
{
	bytes64 finalcount;

	bytes64 tmp;
	tmp.i64 = total << 3;

#ifdef LITTLE_ENDIAN
	finalcount.i32[0] = SWAP32(tmp.i32[1]);
	finalcount.i32[1] = SWAP32(tmp.i32[0]);
#else
	finalcount.i64 = tmp.i64;
#endif
	
	// Copy padding
	Update(sha1_padding, ((64 - 9 - (uint32)total ) & 0x3F) + 1);
	
	// Copy number of bits
	Update(finalcount.i8, 8);

	// Copy final result to the digest.
	((uint32*)digest)[0] = SWAP32(state[0]);
	((uint32*)digest)[1] = SWAP32(state[1]);
	((uint32*)digest)[2] = SWAP32(state[2]);
	((uint32*)digest)[3] = SWAP32(state[3]);
	((uint32*)digest)[4] = SWAP32(state[4]);

}

void SHA1::Hash(const void *mem, size_t length, byte *digest)
{
	SHA1 sha;
	sha.Init();
	sha.Update((byte*)mem, length);
	sha.Finish(digest);
}
