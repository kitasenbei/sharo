#!/usr/bin/env python3
"""Cowllision Benchmark - Python/Pygame (for comparison with Sharo)
O(n²) collision detection between all cows"""

import pygame
import random
import math

WIDTH, HEIGHT = 800, 600
COW_SIZE = 24

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
            random.randint(COW_SIZE, WIDTH - COW_SIZE),
            random.randint(COW_SIZE, HEIGHT - COW_SIZE),
            random.random() * 6.0 - 3.0,
            random.random() * 6.0 - 3.0
        )
        cows.append(cow)

def check_collisions():
    count = len(cows)
    for i in range(count):
        for j in range(i + 1, count):
            cow_a = cows[i]
            cow_b = cows[j]

            dx = cow_b.x - cow_a.x
            dy = cow_b.y - cow_a.y
            dist = math.sqrt(dx * dx + dy * dy)

            if dist < COW_SIZE and dist > 0.1:
                # Normalize
                nx = dx / dist
                ny = dy / dist

                # Separate
                overlap = (COW_SIZE - dist) / 2.0
                cow_a.x -= nx * overlap
                cow_a.y -= ny * overlap
                cow_b.x += nx * overlap
                cow_b.y += ny * overlap

                # Reflect velocities
                dvx = cow_a.vx - cow_b.vx
                dvy = cow_a.vy - cow_b.vy
                dvn = dvx * nx + dvy * ny

                cow_a.vx -= dvn * nx
                cow_a.vy -= dvn * ny
                cow_b.vx += dvn * nx
                cow_b.vy += dvn * ny

def update_cows():
    for cow in cows:
        cow.x += cow.vx
        cow.y += cow.vy

        if cow.x < COW_SIZE // 2:
            cow.x = COW_SIZE // 2
            cow.vx = -cow.vx
        if cow.x > WIDTH - COW_SIZE // 2:
            cow.x = WIDTH - COW_SIZE // 2
            cow.vx = -cow.vx
        if cow.y < COW_SIZE // 2:
            cow.y = COW_SIZE // 2
            cow.vy = -cow.vy
        if cow.y > HEIGHT - COW_SIZE // 2:
            cow.y = HEIGHT - COW_SIZE // 2
            cow.vy = -cow.vy

def draw_cows(screen, texture):
    half = COW_SIZE // 2
    for cow in cows:
        screen.blit(texture, (cow.x - half, cow.y - half))

def main():
    pygame.init()
    screen = pygame.display.set_mode((WIDTH, HEIGHT))
    pygame.display.set_caption("Cowllision - Python/Pygame")
    clock = pygame.time.Clock()

    try:
        texture = pygame.image.load("assets/cow.bmp").convert_alpha()
        texture = pygame.transform.scale(texture, (COW_SIZE, COW_SIZE))
    except:
        texture = pygame.Surface((COW_SIZE, COW_SIZE))
        texture.fill((139, 69, 19))

    print("Cowllision Benchmark - Running...")
    print("O(n^2) collision detection")

    add_cows(50)

    running = True
    fps = 120

    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN:
                if event.key == pygame.K_ESCAPE:
                    running = False

        add_cows(5)
        update_cows()
        check_collisions()

        fps = int(clock.get_fps())
        if fps > 0 and fps < 60:
            running = False

        screen.fill((30, 30, 50))
        draw_cows(screen, texture)
        pygame.display.flip()
        clock.tick()

    collision_checks = len(cows) * (len(cows) - 1) // 2

    print("=== COWLLISION RESULTS (Python/Pygame) ===")
    print(f"Cows: {len(cows)}")
    print(f"Collision checks/frame: {collision_checks}")
    print(f"FPS: {fps}")

    pygame.quit()

if __name__ == "__main__":
    main()
