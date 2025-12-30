#!/usr/bin/env python3
"""Cowmark Benchmark - Python/Pygame (for comparison with Sharo)"""

import pygame
import random

WIDTH, HEIGHT = 800, 600
GRAVITY = 0.5

class Cow:
    __slots__ = ['x', 'y', 'vx', 'vy']
    def __init__(self, x, y, vx, vy):
        self.x = x
        self.y = y
        self.vx = vx
        self.vy = vy

cows = []

def add_cows(count):
    for _ in range(count):
        cow = Cow(
            random.randint(0, WIDTH - 32),
            random.randint(0, HEIGHT // 2),
            random.random() * 10.0 - 5.0,
            random.random() * 5.0
        )
        cows.append(cow)

def update_cows():
    for cow in cows:
        cow.x += cow.vx
        cow.y += cow.vy
        cow.vy += GRAVITY

        if cow.x < 0:
            cow.x = 0
            cow.vx = -cow.vx
        if cow.x > WIDTH - 32:
            cow.x = WIDTH - 32
            cow.vx = -cow.vx
        if cow.y > HEIGHT - 32:
            cow.y = HEIGHT - 32
            cow.vy = -cow.vy * 0.85
        if cow.y < 0:
            cow.y = 0
            cow.vy = -cow.vy

def draw_cows(screen, texture):
    for cow in cows:
        screen.blit(texture, (cow.x, cow.y))

def main():
    pygame.init()
    screen = pygame.display.set_mode((WIDTH, HEIGHT))
    pygame.display.set_caption("Cowmark - Python/Pygame")
    clock = pygame.time.Clock()

    # Load cow texture
    try:
        texture = pygame.image.load("assets/cow.bmp").convert_alpha()
        texture = pygame.transform.scale(texture, (32, 32))
    except:
        # Fallback: draw a simple rect
        texture = pygame.Surface((32, 32))
        texture.fill((139, 69, 19))

    print("Cowmark Benchmark - Running...")

    add_cows(100)

    running = True
    fps = 120

    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN:
                if event.key == pygame.K_ESCAPE:
                    running = False

        # Add more cows every frame
        add_cows(100)

        update_cows()

        # Get FPS from clock
        fps = int(clock.get_fps())

        # Stop when FPS drops below 60
        if fps > 0 and fps < 60:
            running = False

        screen.fill((50, 120, 200))
        draw_cows(screen, texture)
        pygame.display.flip()
        clock.tick()  # No limit, just track time

    print("=== COWMARK RESULTS (Python/Pygame) ===")
    print(f"Cows: {len(cows)}")
    print(f"FPS: {fps}")

    pygame.quit()

if __name__ == "__main__":
    main()
