#!/usr/bin/env python3
"""Fireworks Demo - Python/Pygame-ce (for comparison with Sharo version)"""

import pygame
import random
import math

WIDTH, HEIGHT = 1280, 720
MAX_PARTICLES = 5000
MAX_ROCKETS = 20
GRAVITY = 0.15
DRAG = 0.98

# Particle arrays (same structure as Sharo version)
px, py = [0.0] * MAX_PARTICLES, [0.0] * MAX_PARTICLES
pvx, pvy = [0.0] * MAX_PARTICLES, [0.0] * MAX_PARTICLES
pr, pg, pb = [0] * MAX_PARTICLES, [0] * MAX_PARTICLES, [0] * MAX_PARTICLES
plife = [0] * MAX_PARTICLES
pcount = 0

# Rocket arrays
rx, ry = [0.0] * MAX_ROCKETS, [0.0] * MAX_ROCKETS
rvx, rvy = [0.0] * MAX_ROCKETS, [0.0] * MAX_ROCKETS
rr, rg, rb = [0] * MAX_ROCKETS, [0] * MAX_ROCKETS, [0] * MAX_ROCKETS
rcount = 0

def launch_rocket(x):
    global rcount
    if rcount >= MAX_ROCKETS:
        return
    rx[rcount] = float(x)
    ry[rcount] = float(HEIGHT)
    rvx[rcount] = random.random() * 4.0 - 2.0
    rvy[rcount] = -12.0 - random.random() * 4.0

    colors = [
        (255, 100, 100), (100, 255, 100), (100, 100, 255),
        (255, 255, 100), (255, 100, 255), (100, 255, 255)
    ]
    c = random.choice(colors)
    rr[rcount], rg[rcount], rb[rcount] = c
    rcount += 1

def explode(x, y, r, g, b):
    global pcount
    num_particles = 80 + random.randint(0, 40)

    for _ in range(num_particles):
        if pcount >= MAX_PARTICLES:
            return

        # Find dead slot
        slot = pcount
        for j in range(pcount):
            if plife[j] <= 0:
                slot = j
                break
        if slot == pcount:
            pcount += 1

        px[slot] = x
        py[slot] = y
        angle = random.random() * 6.28318
        speed = 2.0 + random.random() * 6.0
        pvx[slot] = speed * math.cos(angle)
        pvy[slot] = speed * math.sin(angle)
        pr[slot] = max(0, min(255, r + random.randint(-15, 15)))
        pg[slot] = max(0, min(255, g + random.randint(-15, 15)))
        pb[slot] = max(0, min(255, b + random.randint(-15, 15)))
        plife[slot] = 60 + random.randint(0, 40)

def update_rockets():
    global rcount
    i = 0
    while i < rcount:
        rx[i] += rvx[i]
        ry[i] += rvy[i]
        rvy[i] += GRAVITY

        if rvy[i] > -2.0:
            explode(rx[i], ry[i], rr[i], rg[i], rb[i])
            rcount -= 1
            if i < rcount:
                rx[i], ry[i] = rx[rcount], ry[rcount]
                rvx[i], rvy[i] = rvx[rcount], rvy[rcount]
                rr[i], rg[i], rb[i] = rr[rcount], rg[rcount], rb[rcount]
                i -= 1
        i += 1

def update_particles():
    for i in range(pcount):
        if plife[i] > 0:
            px[i] += pvx[i]
            py[i] += pvy[i]
            pvx[i] *= DRAG
            pvy[i] = pvy[i] * DRAG + GRAVITY
            plife[i] -= 1

def draw(screen):
    # Draw particles
    for i in range(pcount):
        if plife[i] > 0:
            alpha = min(255, plife[i] * 4)
            size = 2 + plife[i] // 30
            surf = pygame.Surface((size, size), pygame.SRCALPHA)
            surf.fill((pr[i], pg[i], pb[i], alpha))
            screen.blit(surf, (px[i] - size//2, py[i] - size//2))

    # Draw rockets
    for i in range(rcount):
        pygame.draw.rect(screen, (rr[i], rg[i], rb[i]),
                        (rx[i] - 2, ry[i] - 4, 4, 8))
        pygame.draw.rect(screen, (255, 200, 100),
                        (rx[i] - 1, ry[i] + 4, 2, 10))

def main():
    global pcount, rcount

    pygame.init()
    screen = pygame.display.set_mode((WIDTH, HEIGHT))
    pygame.display.set_caption("Python/Pygame Fireworks")
    clock = pygame.time.Clock()
    font = pygame.font.SysFont("monospace", 20)

    running = True
    auto_launch = 0

    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN:
                if event.key == pygame.K_ESCAPE:
                    running = False
                elif event.key == pygame.K_SPACE:
                    launch_rocket(random.randint(100, WIDTH - 100))
            elif event.type == pygame.MOUSEBUTTONDOWN:
                launch_rocket(random.randint(100, WIDTH - 100))

        # Auto-launch
        auto_launch += 1
        if auto_launch > 60:
            if random.randint(0, 100) < 30:
                launch_rocket(random.randint(100, WIDTH - 100))
            auto_launch = 0

        update_rockets()
        update_particles()

        # Clear
        screen.fill((10, 10, 30))

        draw(screen)

        # FPS display
        fps = int(clock.get_fps())
        pygame.draw.rect(screen, (0, 0, 0), (5, 5, 180, 55))
        fps_text = font.render(f"FPS: {fps}", True, (0, 255, 0))
        part_text = font.render(f"Particles: {pcount}", True, (255, 255, 255))
        screen.blit(fps_text, (10, 10))
        screen.blit(part_text, (10, 32))

        pygame.display.flip()
        # clock.tick(60)  # Uncapped for comparison

    pygame.quit()

if __name__ == "__main__":
    main()
