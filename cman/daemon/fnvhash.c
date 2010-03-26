#include <stdint.h>
#include "fnvhash.h"

/***
 *
 * Fowler/Noll/Vo hash
 *
 * The basis of this hash algorithm was taken from an idea sent
 * as reviewer comments to the IEEE POSIX P1003.2 committee by:
 *
 *      Phong Vo (http://www.research.att.com/info/kpv/)
 *      Glenn Fowler (http://www.research.att.com/~gsf/)
 *
 * In a subsequent ballot round:
 *
 *      Landon Curt Noll (http://www.isthe.com/chongo/)
 *
 * improved on their algorithm.  Some people tried this hash
 * and found that it worked rather well.  In an EMail message
 * to Landon, they named it the ``Fowler/Noll/Vo'' or FNV hash.
 *
 * FNV hashes are designed to be fast while maintaining a low
 * collision rate. The FNV speed allows one to quickly hash lots
 * of data while maintaining a reasonable collision rate.  See:
 *
 *      http://www.isthe.com/chongo/tech/comp/fnv/index.html
 *
 * for more details as well as other forms of the FNV hash.
 ***
 *
 * To use the recommended 32 bit FNV-1a hash, pass FNV1_32A_INIT as the
 * Fnv32_t hashval argument to fnv_32a_buf() or fnv_32a_str().
 *
 ***
 *
 * Please do not copyright this code.  This code is in the public domain.
 *
 * LANDON CURT NOLL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO
 * EVENT SHALL LANDON CURT NOLL BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
 * USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * By:
 *	chongo <Landon Curt Noll> /\oo/\
 *      http://www.isthe.com/chongo/
 *
 * Share and Enjoy!	:-)
 */

/* 
 * Modified to be a little more simple to understand and to provide a 16 bit
 * value rather then 32 bit for cluster id generation
 *
 * sdake@redhat.com
 */

/* 32 bit magic FNV-1a prime */
#define FNV_32_PRIME ((uint32_t)0x01000193)

/* Default initialization for FNV-1a */
#define FNV_32_INIT ((uint32_t)0x811c9dc5)

uint16_t fnv_hash(char *str)
{
	unsigned char *s = (unsigned char *)str;
	uint32_t hval = FNV_32_INIT;
	uint32_t ret;

	/*
	* FNV-1a hash each octet in the buffer
	*/
	while (*s) {
		/*
		 * xor the bottom with the current octet
		 */
		hval ^= (uint32_t)*s++;
		/*
		 * multiply by the 32 bit FNV magic prime mod 2^32
		 */
		hval *= FNV_32_PRIME;
	}

	/*
	 * Use XOR folding as recommended by authors of algorithm
	 * to create a different hash size that is a power of two
	 */
	ret = (hval >> 16) ^ (hval & 0xFFFF);

	return (ret);
}
