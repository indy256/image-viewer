# JP2 test images

These small, synthetic, losslessly encoded images are test fixtures for this project.

| File | Dimensions | Encoded samples |
| --- | --- | --- |
| red.jp2 | 64 x 48 | RGB: 255, 0, 0 |
| blue.jp2 | 64 x 48 | RGB: 0, 0, 255 |
| gray.jp2 | 64 x 48 | 8-bit grayscale: 128 |
| rgba.jp2 | 4 x 4 | RGBA: 200, 100, 50, 128; straight alpha |
| gray16.jp2 | 4 x 4 | Unsigned 16-bit grayscale: 32768 |
| signed16.jp2 | 4 x 4 | Signed 16-bit grayscale: -32768 |
| sycc420.jp2 | 4 x 4 | Y, Cb, Cr: 128, 128, 128; 4:2:0 subsampling |
| cmyk.jp2 | 4 x 4 | C, M, Y, K: 0, 255, 255, 0 |

The RGB and 8-bit grayscale fixtures were generated with JasPer; the other
fixtures were generated with OpenJPEG 2.5.4. Decoding tests compare against the
known pixel values above and require no encoder at runtime.
