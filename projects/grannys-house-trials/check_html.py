from bs4 import BeautifulSoup

soup = BeautifulSoup(open("books/downloads/cpp-guidelines.html", encoding='utf-8').read(), 'lxml')
headings = soup.find_all(['h1', 'h2','h3', 'h4'])
print(f"Found {len(headings)} total headings")
print("\nFirst 15 headings:")
for i, h in enumerate(headings[:15]):
  print(f"{i+1}. Tag: {h.name}, ID: {h.get('id')}, Text: {h.text[:70]}")
