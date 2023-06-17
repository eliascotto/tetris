#ifdef __APPLE__
    #include <SDL2/SDL.h>
    #include <SDL2_ttf/SDL_ttf.h>
#else 
    #include <SDL2/SDL.h>
    #include <SDL2/SDL_ttf.h>
#endif /* __APPLE__ */

#include <exception>
#include <iostream>
#include <cmath>
#include <vector>
#include <array>
#include <random>

#define MAX_KEYBOARD_KEYS 350

//------------------------------------------------------------------------------------
// Constants Definition
//------------------------------------------------------------------------------------
// Window
static const int screenWidth = 600;
static const int screenHeight = 480;

// Font
static const std::string defaultFontPath = "assets/fonts/Pixel.ttf";

// Square size (could be computed based on the screen size if we want to)
static const int squareSize = 20;
// Grid sized according to google
static const int gridCols = 10;
static const int gridRows = 20;

// Grid position x,y
static const int gridPosX = 120;
static const int gridPosY = 30;
// Next block preview
static const int nextBlockPreviewDistance = 50;

// Game speed (lower is faster)
static const int gravitySpeed = 30;
static const int lateralSpeed = 8;
static const int rotatingSpeed = 8;
static const int fadingTime = 50;
static const int speedyGravityDelay = 40;

//------------------------------------------------------------------------------------
// Data
//------------------------------------------------------------------------------------
enum BlockState { EMPTY, MOVING, BLOCK, WALL, FADING };

enum BlockType { I, O, T, S, Z, J, L, BLOCKTYPE_COUNT };

std::vector<std::vector<std::vector<int> > > blockShapes = {
    { // I
        {0, 0, 0, 0},
        {1, 1, 1, 1},
        {0, 0, 0, 0},
        {0, 0, 0, 0},
    },
    { // O
        {0, 0, 0, 0},
        {0, 1, 1, 0},
        {0, 1, 1, 0},
        {0, 0, 0, 0},
    },
    { // T
        {0, 0, 0, 0},
        {0, 1, 1, 1},
        {0, 0, 1, 0},
        {0, 0, 0, 0},
    },
    { // S
        {0, 0, 0, 0},
        {0, 0, 1, 1},
        {0, 1, 1, 0},
        {0, 0, 0, 0},
    },
    { // Z
        {0, 0, 0, 0},
        {0, 1, 1, 0},
        {0, 0, 1, 1},
        {0, 0, 0, 0},
    },
    { // J
        {0, 0, 0, 0},
        {0, 1, 1, 1},
        {0, 1, 0, 0},
        {0, 0, 0, 0},
    },
    { // L
        {0, 0, 0, 0},
        {0, 1, 1, 1},
        {0, 0, 0, 1},
        {0, 0, 0, 0},
    },
};

//------------------------------------------------------------------------------------
// Utils
//------------------------------------------------------------------------------------
int
randomNumber (int min, int max)
{
    std::random_device rd;  // Random device engine, to generate a seed
    std::mt19937 mt(rd());  // Initialize Mersenne Twister pseudo-random number generator
    std::uniform_int_distribution<int> dist(min, max);  // Uniform distribution between min and max
    return dist(mt);  // Generate and return a random number
}

//------------------------------------------------------------------------------------
// Classes
//------------------------------------------------------------------------------------
// Class that simply keep tracking of each pressed key into a boolean vector.
class InputManager {
private:
    std::vector<bool> keymap;

public:
    InputManager() : keymap(MAX_KEYBOARD_KEYS, false) {}

    void handlerInput (SDL_Event event)
    {
        if (event.type == SDL_KEYDOWN) {
            keymap[event.key.keysym.scancode] = true;
        }
        else if (event.type == SDL_KEYUP) {
            keymap[event.key.keysym.scancode] = false;
        }
    }

    bool isKeyPressed (SDL_Scancode code) { return keymap[code]; }
};

class FontManager {
private:
    TTF_Font* font;

public:
    FontManager (const char* fontPath, int size) {
        // Font opening requires a size, which cannot be changed afterwards.
        // So to have multiple text size rendering, you need to create 
        // multiple FontManager objects, each with a different size.
        font = TTF_OpenFont(fontPath, size);
        if (!font) {
            std::cout << TTF_GetError() << std::endl;
            throw std::runtime_error("Failed to load font");
        }
    }

    ~FontManager () { TTF_CloseFont(font); }

    void drawText (SDL_Renderer* renderer, char* text, int x, int y) { 
        drawText(renderer, text, {0, 0, 0, 255}, x, y); 
    }
    void drawText (SDL_Renderer* renderer, char* text, SDL_Color color, int x, int y);
};

void
FontManager::drawText (SDL_Renderer* renderer, char* text, SDL_Color color, int x, int y)
{
    SDL_Surface* surface = TTF_RenderText_Solid(font, text, color);
    if (surface == nullptr) {
        throw std::runtime_error("Failed to create text surface");
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture == nullptr) {
        SDL_FreeSurface(surface);
        throw std::runtime_error("Failed to create text texture");
    }

    SDL_Rect dstrect = { x, y, surface->w, surface->h };
    SDL_RenderCopy(renderer, texture, NULL, &dstrect);

    SDL_FreeSurface(surface);
    SDL_DestroyTexture(texture);
}

class Block {
public:
    BlockType type;
    std::vector<std::vector<int> > shape;
    int posX, posY;

    Block (BlockType t);

    void setPosition (int x, int y) { posX = x; posY = y; }
    void rotate () { shape = rotationPreview(); }
    void moveDown () { posY++; };
    void moveLeft () { posX--; };
    void moveRight () { posX++; };
    std::vector<std::vector<int> > rotationPreview ();
};

Block::Block (BlockType t)
{
    int idx = static_cast<int>(t);
    type = t;
    shape = blockShapes[idx];
    posX = 0;
    posY = 0;
}

std::vector<std::vector<int> >
Block::rotationPreview ()
{
    // Avoid rotating O shape
    if (type == O) return shape;

    int n = shape.size();
    std::vector<std::vector<int>> rotated(n, std::vector<int>(n, 0));

    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            rotated[j][n - i - 1] = shape[i][j];  // Rotate vector
        }
    }

    return rotated;
}

//
// To keep track of the current moving block and the game grid, we use
// two different matrixes. The grid is default to 10x20, while the matrix
// for the moving block is a 4x4. 
// The movingBlock object keeps track of the matrix, plus the position of the block
// in the grid (x,y). To do collision detection, we iterate over
// the block matrix, adding the position and checking if in the grid matrix,
// the cell is EMPTY or is a WALL/BLOCK and in case, is a collision.
//
class Tetris {
private:
    // Game
    bool gameOver;
    int  score;
    int  gravityMovementCounter;
    int  lateralMovementCounter;
    int  rotatingMovementCounter;
    int  speedyGravityMovementCounter;
    int  rowsFadingCounter;

    std::vector<int> rowsToDelete; 

    InputManager inputManager;
    FontManager* gameOverFont;
    FontManager* otherFont;

    // Grid
    std::vector< std::vector<int> > grid;
    int colsN, rowsN;

    // Blocks
    std::unique_ptr<Block> movingBlock;
    std::unique_ptr<Block> nextBlock;

    std::unique_ptr<Block> createRandomBlock ();
    void setNewBlocks ();
    void clearGrid (bool fullClean = false);
    void initialize ();
    void addCurrentBlockToGrid (bool addAsBlock);
    bool solveVerticalCollision ();
    void solveHorizontalCollision ();
    void solveRotationCollision ();
    void checkCompletedRows ();
    void checkGameOver ();
    void removeCompletedRows ();

public:
    Tetris (int cols, int rows, const char* fontPath);
    ~Tetris () { delete gameOverFont; delete otherFont; };

    void update ();
    void draw (SDL_Renderer* renderer);
    void handleInput (SDL_Event event) { inputManager.handlerInput(event); }
};

Tetris::Tetris (int cols, int rows, const char* fontPath)
{
    colsN = cols; rowsN = rows;
    gameOverFont = new FontManager(fontPath, 24);
    otherFont = new FontManager(fontPath, 12);
    initialize();
}

void
Tetris::update ()
{
    if (gameOver) {
        if (inputManager.isKeyPressed(SDL_SCANCODE_RETURN))
            initialize();
        else
            return;
    }

    if (!rowsToDelete.empty()) {
        // Increment fading counter for fading effect
        rowsFadingCounter++;

        if (rowsFadingCounter >= fadingTime) {
            removeCompletedRows();

            rowsFadingCounter = 0;
            rowsToDelete.clear();
        }
        return;
    }

    if (movingBlock == nullptr)
        setNewBlocks(); // Create a new moving block

    gravityMovementCounter++;
    // lateralMovementCounter++;
    speedyGravityMovementCounter++;

    bool verticalCollision = false, horizontalCollision = false;

    if (inputManager.isKeyPressed(SDL_SCANCODE_LEFT) || 
        inputManager.isKeyPressed(SDL_SCANCODE_RIGHT))
        lateralMovementCounter++;

    if (inputManager.isKeyPressed(SDL_SCANCODE_UP))
        rotatingMovementCounter++;

    if (inputManager.isKeyPressed(SDL_SCANCODE_DOWN) &&
        speedyGravityMovementCounter >= speedyGravityDelay) {
        gravityMovementCounter += gravitySpeed; // Increase the counter to speed up the block
    }

    // Check vertical movement for collision and completed row, if counter is more than treshold
    if (gravityMovementCounter >= gravitySpeed) {
       verticalCollision = solveVerticalCollision(); // Check collision with bottom wall and other blocks

        checkCompletedRows();
        // Reset the counter and then wait for the next cycle to move the block again
        gravityMovementCounter = 0;
    }

    // Check horizontal movement for collision, otherwhise move in decided direction
    if (lateralMovementCounter >= lateralSpeed) {
        solveHorizontalCollision();
        lateralMovementCounter = 0;
    }

    // Check block rotation and rotate otherwise
    if (rotatingMovementCounter >= rotatingSpeed) {
        solveRotationCollision();
        rotatingMovementCounter = 0;
    }

    clearGrid();
    addCurrentBlockToGrid(verticalCollision);

    if (verticalCollision)
        movingBlock = nullptr; // Reset moving block to start with a new one

    checkGameOver();
}

void
Tetris::draw (SDL_Renderer* renderer)
{
    // Clear screen
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);

    if (gameOver) {
        gameOverFont->drawText(renderer, (char*) "Press [enter] to play again", 100, 100);
        std::string scoreText = "SCORE: " + std::to_string(score);
        otherFont->drawText(renderer, (char*) scoreText.c_str(), 250, 150);
    }
    else {
        // Draw blocks
        for (int i = 0; i < grid.size(); i++) {
            for (int j = 0; j < grid[0].size(); j++) {

                SDL_Rect square = {
                    gridPosX + (squareSize * j),
                    gridPosY + (squareSize * i),
                    squareSize,
                    squareSize
                };

                if (grid[i][j] == EMPTY) {
                    SDL_SetRenderDrawColor(renderer, 245, 245, 245, 255);
                    // Draw a block border only
                    SDL_RenderDrawRect(renderer, &square);
                }
                else {
                    if (grid[i][j] == WALL)
                        SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
                    else if (grid[i][j] == FADING)
                        SDL_SetRenderDrawColor(renderer, 0, 150, 0, 255);
                    else
                        SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
                    // Draw a colored block
                    SDL_RenderFillRect(renderer, &square);
                }
            }
        }

        int gridWidth = squareSize * (colsN + 2);
        // Draw next block preview
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {

                SDL_Rect square = {
                    gridPosX + nextBlockPreviewDistance + gridWidth + (squareSize * j),
                    gridPosY + 30 + (squareSize * i),
                    squareSize,
                    squareSize
                };

                if (nextBlock->shape[i][j] == EMPTY) {
                    SDL_SetRenderDrawColor(renderer, 245, 245, 245, 255);
                    SDL_RenderDrawRect(renderer, &square);
                }
                else {
                    SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
                    SDL_RenderFillRect(renderer, &square);
                }
            }
        }
        otherFont->drawText(
            renderer, 
            (char*) "NEXT BLOCK",
            gridPosX + nextBlockPreviewDistance + gridWidth, 
            gridPosY
        );

        int blockPreviewHeight = squareSize * 4;
        std::string scoreText = "SCORE: " + std::to_string(score);
        otherFont->drawText(
            renderer, 
            (char*) scoreText.c_str(),
            gridPosX + nextBlockPreviewDistance + gridWidth, 
            gridPosY + 30 + blockPreviewHeight + 30
        );
    }

    SDL_RenderPresent(renderer);
}

void
Tetris::initialize ()
{
    // + 1 since we include the bottom wall, + 2 for the lateral walls
    grid.resize(rowsN + 1, std::vector<int>(colsN + 2, EMPTY));
    rowsToDelete.clear();

    clearGrid(true);
    setNewBlocks();

    score = 0;
    gameOver = false;
    gravityMovementCounter = 0;
    lateralMovementCounter = 0;
    rotatingMovementCounter = 0;
    rowsFadingCounter = 0;
    speedyGravityMovementCounter = 0;
}

void
Tetris::checkCompletedRows ()
{
    int squareCounter;
    for (int i = 0; i < rowsN; i++) {
        squareCounter = 0;

        for (int j = 0; j < colsN; j++) {
            if (grid[i][j + 1] == BLOCK)
                squareCounter++;
        }

        if (squareCounter == colsN) {
            // Add rows to array for score, then set the row to FADING
            rowsToDelete.push_back(i);

            for (int j = 0; j < colsN; j++)
                grid[i][j + 1] = FADING;
        }
    }

    if (!rowsToDelete.empty()) {
        switch(rowsToDelete.size()) {
            case 1: score += 40;   break;
            case 2: score += 100;  break;
            case 3: score += 300;  break;
            case 4: score += 1200; break;
        }
    }
}

void
Tetris::removeCompletedRows ()
{
    for (const auto &rowIndex : rowsToDelete) {
        for (int j = rowIndex; j > 0; j--)
            grid[j] = grid[j - 1];

        grid[0] = std::vector<int>(colsN + 2, 0); // Add firt row of 0
        grid[0][0] = grid[0][colsN + 1] = WALL;   // Set lateral walls
    }
}

void
Tetris::solveRotationCollision ()
{
    bool collision = false;
    int blockPosX = movingBlock->posX, blockPosY = movingBlock->posY;
    // Use a temp matrix to preview the rotation and check if it collides
    std::vector<std::vector<int> > rotatedShape = movingBlock->rotationPreview();

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            int sideBlock = grid[blockPosY + i][blockPosX + j];

            if (rotatedShape[i][j] == MOVING && 
                (sideBlock == BLOCK || sideBlock == WALL)) {
                collision = true;
            }
        }
    }

    if (!collision)
        movingBlock->rotate();
}

void
Tetris::solveHorizontalCollision ()
{
    bool isLeftPressed = inputManager.isKeyPressed(SDL_SCANCODE_LEFT);
    bool isRightPressed = inputManager.isKeyPressed(SDL_SCANCODE_RIGHT);
    int blockPosX = movingBlock->posX, blockPosY = movingBlock->posY;
    bool collision = false;
        
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            int sideBlock;
            if (isLeftPressed)
                sideBlock = grid[blockPosY + i][blockPosX + j - 1];
            else if (isRightPressed)
                sideBlock = grid[blockPosY + i][blockPosX + j + 1];
            
            // Collision if block on left/right side is a BLOCK/WALL
            if (movingBlock->shape[i][j] == MOVING && (sideBlock == BLOCK || sideBlock == WALL)) {
                collision = true;
            }
        }
    }

    if (!collision) {
        if (isLeftPressed)
            movingBlock->moveLeft();
        else if (isRightPressed)
            movingBlock->moveRight();
    }
}

bool 
Tetris::solveVerticalCollision ()
{
    int blockPosX = movingBlock->posX, blockPosY = movingBlock->posY;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            // If block below a moving block is BLOCK or WALL, collision is detected
            if (movingBlock->shape[i][j] == MOVING &&
                (grid[blockPosY + i + 1][blockPosX + j] == BLOCK || 
                 grid[blockPosY + i + 1][blockPosX + j] == WALL)) {
                return true;
            }
        }
    }

    movingBlock->moveDown(); // If no collision, move the block down
    return false;
}

void
Tetris::checkGameOver ()
{
    for (int j = 0; j < 2; j++) {
        for (int i = 0; i < colsN; i++) {
            if (grid[j][i] == BLOCK)
                gameOver = true;
        }
    }
}

void
Tetris::setNewBlocks ()
{
    if (nextBlock == NULL)
        movingBlock = createRandomBlock();
    else
        movingBlock = std::move(nextBlock); // swap block pointers

    // Block start from x: half, y: 0
    int squaresX = std::floor(gridCols / 2) - 2;
    movingBlock->setPosition(squaresX, 0);

    nextBlock = createRandomBlock();
    speedyGravityMovementCounter = 0; // Reset the speed counter
}

void
Tetris::addCurrentBlockToGrid (bool addAsBlock)
{
    int blockPosX = movingBlock->posX, blockPosY = movingBlock->posY;

    for (int i = 0; i < 4; i++) {
        if ((blockPosY + i) >= grid.size()) // Prevent outiside grid indexing
            return;
        
        for (int j = 0; j < 4; j++) {
            if (addAsBlock && movingBlock->shape[i][j] == MOVING)
                // Make moving block a fixed one
                grid[blockPosY + i][blockPosX + j] = BLOCK;
            else
                // Transfer the moving block to the grid
                grid[blockPosY + i][blockPosX + j] += movingBlock->shape[i][j]; 
        }
    }
}

void
Tetris::clearGrid (bool fullClean)
{
    for (int i = 0; i < grid.size(); i++) {
        for (int j = 0; j < grid[0].size(); j++) {
            if (grid[i][j] == MOVING || fullClean)
                grid[i][j] = EMPTY;
        }
        grid[i][0] = grid[i][colsN + 1] = WALL;
    }

    for (int i = 1; i < colsN + 1; i++) {
        grid[rowsN][i] = WALL;
    }
}

std::unique_ptr<Block>
Tetris::createRandomBlock ()
{
    int blockIdx = randomNumber(0, BLOCKTYPE_COUNT - 1);
    BlockType type = static_cast<BlockType>(blockIdx);
    return std::make_unique<Block>(type);
}

//------------------------------------------------------------------------------------
// Functions declarations
//------------------------------------------------------------------------------------
void initSDL();
void quit();

//------------------------------------------------------------------------------------
// Variables
//------------------------------------------------------------------------------------
SDL_Window*   window;
SDL_Renderer* renderer;
TTF_Font*     font;

//------------------------------------------------------------------------------------
// Main
//------------------------------------------------------------------------------------
int
main (int argc, char* argv[])
{
    // Clear SDL2 stuff before termination
    std::atexit(quit);
    std::srand((unsigned) std::time(0));

    initSDL();

    Tetris game(gridCols, gridRows, defaultFontPath.c_str());

    while (1) {
        SDL_Event evt;

        while (SDL_PollEvent(&evt)) {
            // Exit or let the game object handle keyboard input
            if (evt.type == SDL_QUIT)
                exit(0);
            else 
                game.handleInput(evt);
        }

        game.update();
        game.draw(renderer);

        SDL_Delay(10);
    }

    return 0;
}


void
initSDL ()
{
    if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
        std::cout << "Error initializing SDL: " << SDL_GetError() << std::endl;
        system("pause");
        exit(-1);
    }

    if (TTF_Init() < 0) {
        std::cout << "Error initializing SDL_ttf: " << SDL_GetError() << std::endl;
        system("pause");
        exit(-1);
    }

    window = SDL_CreateWindow(
        "Tetris",                           // window title
        SDL_WINDOWPOS_UNDEFINED,           // initial x position
        SDL_WINDOWPOS_UNDEFINED,           // initial y position
        screenWidth,                      // width, in pixels
        screenHeight,                     // height, in pixels
        SDL_WINDOW_SHOWN                  // flags - see below
    );
    if (!window) {
        std::cout << "Error creating window: " << SDL_GetError()  << std::endl;
        system("pause");
        exit(-1);
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        std::cout << "Error creating renderer: " << SDL_GetError() << std::endl;
        exit(-1);
    }

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);
}

void
quit ()
{
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
}
