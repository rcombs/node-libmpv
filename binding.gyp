{
  "targets": [
    {
      "target_name": "libmpv",
      "sources": [ "libmpv.c" ],
      "libraries": [
        '<!@(pkg-config mpv --libs)'
      ],
      "include_dirs": [
        '<!@(pkg-config mpv --cflags-only-I | sed s/-I//g)'
      ],
    }
  ]
}
