<svg xmlns="http://www.w3.org/2000/svg" width="{{svg_width}}" height="{{svg_height}}" viewBox="0 0 {{svg_width}} {{svg_height}}">
  <rect width="100%" height="100%" fill="#fffdf8"/>
  <rect x="{{margin_left}}" y="{{margin_top}}" width="{{plot_width}}" height="{{plot_height}}" fill="#fffdf8" stroke="#d8d2c6"/>
  <text x="{{margin_left}}" y="28" font-size="22" font-family="serif" fill="#1f2933">{{title}}</text>
  <text x="{{margin_left}}" y="{{x_axis_label_y}}" font-size="16" font-family="sans-serif" fill="#374151">n</text>
  <text x="24" y="{{y_axis_label_y}}" font-size="16" font-family="sans-serif" fill="#374151" transform="rotate(-90 24 {{y_axis_label_y}})">GFLOPs/s</text>
{{y_ticks}}{{x_ticks}}{{series_layers}}</svg>
