
import numpy as np
from PIL import Image


def generate_cracks(
    output_path="output.png",
    noise_path="noise_map.png",
    width=512,
    height=512,
    cell_size=40,
    crack_width=2.0,
    seed=150
):
    rng = np.random.default_rng(seed)

    count = int(width * height / cell_size**2 * 2)
    points = rng.uniform(
        [0, 0],
        [width, height],
        size=(count, 2)
    )

    yy, xx = np.mgrid[0:height, 0:width]
    nearest = np.full((height, width), np.inf)
    second_nearest = np.full((height, width), np.inf)

    for px, py in points:
        distance = np.sqrt((xx - px) ** 2 + (yy - py) ** 2)

        second_nearest = np.minimum(
            second_nearest,
            np.maximum(nearest, distance)
        )
        nearest = np.minimum(nearest, distance)

    noise = second_nearest - nearest

    noise_normalised = np.clip(
        noise / crack_width * 32, 0, 255
    ).astype(np.uint8)

    Image.fromarray(noise_normalised).save(noise_path)

    image = np.where(noise < crack_width, 0, 255).astype(np.uint8)
    Image.fromarray(image).save(output_path)


if __name__ == "__main__":
    generate_cracks()
