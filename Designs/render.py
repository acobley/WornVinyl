import sys, pathlib
from playwright.sync_api import sync_playwright
d = pathlib.Path(__file__).parent
with sync_playwright() as p:
    b = p.chromium.launch()
    for name in sys.argv[1:]:
        pg = b.new_page(device_scale_factor=2, viewport={"width": 1000, "height": 480})
        pg.goto((d / f"{name}.html").as_uri()); pg.wait_for_timeout(300)
        w, h = pg.evaluate("[document.body.scrollWidth, document.body.offsetHeight]")
        pg.set_viewport_size({"width": w, "height": h})
        pg.screenshot(path=str(d / f"{name}.png"), clip={"x": 0, "y": 0, "width": w, "height": h})
        print(name, w, h, 'wordmark', pg.evaluate("window.wmWidth"))
    b.close()
