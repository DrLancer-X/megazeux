uniform sampler2D baseMap;

varying vec2 vTexcoord;

void main( void )
{
    int x = int(vTexcoord.x * 256.0 * 8.0);
    int y = int(vTexcoord.y * 14.0);

    int cx = x / 8;
    int cy = y / 14;

    vec4 tileinfo = texture2D( baseMap, vec2(float(cy) / 1024.0, (float(cx) + 450.0)/1024.0));
    int ti_byte1 = int(tileinfo.x * 255.0);
    int ti_byte2 = int(tileinfo.y * 255.0);
    int ti_byte3 = int(tileinfo.z * 255.0);
    int ti_byte4 = int(tileinfo.w * 255.0);

    // 00000000 00000000 00000000 00000000
    // CCCCCCCC CCCCCCBB BBBBBBBF FFFFFFFF

    int fg_color = ti_byte1 + int(mod(float(ti_byte2), 2.0)) * 256;
    int bg_color = ti_byte2 / 2 + int(mod(float(ti_byte3), 4.0)) * 128;
    int char_num = ti_byte3 / 4 + ti_byte4 * 64;
    if (fg_color == 272 || bg_color == 272) {
      gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
      return;
    }
    //char_num = mod(char_num, 256);
    
    int px = int(mod(float(x), 8.0));
    int py = int(mod(float(y), 14.0));
    int char_x = int(mod(float(char_num), 128.0)) * 8 + (px / 2 * 2);
    int char_y = char_num / 128 * 14 + py;
    
    int smzx_col;
    if (texture2D(baseMap, vec2(1.0 / 1024.0 * float(char_x), 1.0 / 1024.0 * float(char_y))).x < 0.5) {
      if (texture2D(baseMap, vec2(1.0 / 1024.0 * float(char_x + 1), 1.0 / 1024.0 * float(char_y))).x < 0.5) {
        smzx_col = 0;
      } else {
        smzx_col = 1;
      }
    } else {
      if (texture2D(baseMap, vec2(1.0 / 1024.0 * float(char_x + 1), 1.0 / 1024.0 * float(char_y))).x < 0.5) {
        smzx_col = 2;
      } else {
        smzx_col = 3;
      }
    }

    int mzx_col = bg_color * 16 + fg_color;
    int real_col = int(texture2D( baseMap, vec2(1.0 / 1024.0 * (float(mzx_col) + 256.0 * float(smzx_col)), 449.0/1024.0)).r * 255.0);
    
    gl_FragColor = texture2D( baseMap, vec2(1.0 / 1024.0 * float(real_col), 448.0/1024.0));
}
