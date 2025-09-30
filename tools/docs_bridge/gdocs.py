"""Google Docs API integration with OAuth and service account support."""

from __future__ import annotations
import json
import os
import pathlib
import sys
from typing import Optional, Tuple

from google.auth.transport.requests import Request
from google.oauth2.credentials import Credentials
from google.oauth2 import service_account
from google_auth_oauthlib.flow import InstalledAppFlow
from googleapiclient.discovery import build
from googleapiclient.errors import HttpError

# Google API scopes
SCOPES = [
    'https://www.googleapis.com/auth/documents',
    'https://www.googleapis.com/auth/drive.readonly',
]


def resolve_oauth_client_path(
    cli_path: Optional[str] = None
) -> Tuple[Optional[pathlib.Path], str]:
    """
    Return (path_or_none, source_label) for the OAuth client JSON.

    Search order:
      1) cli_path (from --oauth-client)
      2) $GOOGLE_OAUTH_CLIENT_JSON (absolute path or inline JSON)
      3) XDG path: $XDG_CONFIG_HOME/juce-audio-service/google/oauth_client.json
      4) macOS/Linux default: ~/.config/juce-audio-service/google/oauth_client.json
      5) repo fallback: tools/docs_bridge/oauth_client.json  (if present)

    If $GOOGLE_OAUTH_CLIENT_JSON looks like a JSON object (starts with '{'),
    write it to a temp file under ~/.config/juce-audio-service/google/oauth_client.json
    and return that path (source='env-inline').
    """
    def cfg_dir() -> pathlib.Path:
        base = os.environ.get("XDG_CONFIG_HOME") or os.path.join(pathlib.Path.home(), ".config")
        return pathlib.Path(base) / "juce-audio-service" / "google"

    # 1) CLI
    if cli_path:
        p = pathlib.Path(cli_path).expanduser().resolve()
        if p.exists():
            return p, "cli"
        # fallthrough (still report later)

    # 2) ENV (either path or inline JSON)
    env = os.environ.get("GOOGLE_OAUTH_CLIENT_JSON")
    if env:
        if env.strip().startswith("{"):
            cfg_dir().mkdir(parents=True, exist_ok=True)
            p = cfg_dir() / "oauth_client.json"
            p.write_text(env, encoding="utf-8")
            os.chmod(p, 0o600)
            return p, "env-inline"
        else:
            p = pathlib.Path(env).expanduser().resolve()
            if p.exists():
                return p, "env-path"

    # 3) XDG
    xdg_base = os.environ.get("XDG_CONFIG_HOME")
    if xdg_base:
        p = pathlib.Path(xdg_base) / "juce-audio-service" / "google" / "oauth_client.json"
        if p.exists():
            return p, "xdg"

    # 4) Default ~/.config
    p = pathlib.Path.home() / ".config" / "juce-audio-service" / "google" / "oauth_client.json"
    if p.exists():
        return p, "home-config"

    # 5) Repo fallback
    repo = pathlib.Path(__file__).resolve().parent / "oauth_client.json"
    if repo.exists():
        return repo.resolve(), "repo-fallback"

    return None, "not-found"


def build_installed_app_creds_json_from_env() -> Optional[dict]:
    """
    If GOOGLE_OAUTH_CLIENT_ID and GOOGLE_OAUTH_CLIENT_SECRET are set,
    return a dict shaped like an 'installed' credentials file, else None.
    """
    cid = os.environ.get("GOOGLE_OAUTH_CLIENT_ID")
    csec = os.environ.get("GOOGLE_OAUTH_CLIENT_SECRET")
    if not (cid and csec):
        return None
    return {
        "installed": {
            "client_id": cid,
            "project_id": os.environ.get("GOOGLE_OAUTH_PROJECT_ID", "juce-audio-service"),
            "auth_uri": "https://accounts.google.com/o/oauth2/auth",
            "token_uri": "https://oauth2.googleapis.com/token",
            "auth_provider_x509_cert_url": "https://www.googleapis.com/oauth2/v1/certs",
            "client_secret": csec,
            "redirect_uris": ["http://localhost"]
        }
    }


class GoogleDocsClient:
    """Client for interacting with Google Docs API."""

    def __init__(self, token_path: Optional[str] = None, credentials_path: Optional[str] = None):
        """
        Initialize Google Docs client.

        Args:
            token_path: Path to OAuth token file (default: ~/.config/juce-audio-service/google/token.json)
            credentials_path: Path to OAuth client credentials or service account key
                             (can also be set via GOOGLE_APPLICATION_CREDENTIALS env var)
        """
        self.token_path = token_path or self._default_token_path()
        self.credentials_path = credentials_path or os.getenv('GOOGLE_APPLICATION_CREDENTIALS')
        self.creds = None
        self.docs_service = None
        self.drive_service = None

    @staticmethod
    def _default_token_path() -> str:
        """Get default token path."""
        config_dir = pathlib.Path.home() / '.config' / 'juce-audio-service' / 'google'
        config_dir.mkdir(parents=True, exist_ok=True)
        return str(config_dir / 'token.json')

    def authenticate(
        self,
        oauth_client_path: Optional[str] = None,
        token_override: Optional[str] = None,
        verbose: bool = False
    ) -> bool:
        """
        Authenticate with Google APIs.

        Supports:
        - Service account (via GOOGLE_APPLICATION_CREDENTIALS)
        - OAuth desktop flow (via file or env vars)

        Args:
            oauth_client_path: Optional path to OAuth client JSON
            token_override: Optional override for token path
            verbose: Print detailed credential resolution info

        Returns:
            True if authentication successful, False otherwise
        """
        # Override token path if specified
        if token_override:
            self.token_path = token_override
        elif os.environ.get('GOOGLE_TOKEN_PATH'):
            self.token_path = os.environ['GOOGLE_TOKEN_PATH']

        # Try service account first
        if self.credentials_path and pathlib.Path(self.credentials_path).exists():
            try:
                # Check if it's a service account key
                with open(self.credentials_path, 'r') as f:
                    key_data = json.load(f)
                    if key_data.get('type') == 'service_account':
                        if verbose:
                            print("[gdocs] OAuth mode: service-account")
                            print(f"[gdocs] Credentials source: GOOGLE_APPLICATION_CREDENTIALS")
                            print(f"[gdocs] Credentials path: {self.credentials_path}")
                            print(f"[gdocs] Scopes: {', '.join(SCOPES)}")

                        self.creds = service_account.Credentials.from_service_account_file(
                            self.credentials_path,
                            scopes=SCOPES
                        )
                        self._build_services()
                        return True
            except Exception as e:
                if verbose:
                    print(f"[gdocs] Failed to load service account: {e}")

        # OAuth desktop flow - resolve credentials
        creds_path, creds_source = resolve_oauth_client_path(oauth_client_path)
        env_creds_dict = build_installed_app_creds_json_from_env()

        # Print startup banner
        if verbose:
            print("[gdocs] OAuth mode: installed-app")
            if env_creds_dict:
                print("[gdocs] Credentials source: env-vars")
                print("[gdocs] Credentials path: NONE (using env-provided client id/secret)")
            else:
                print(f"[gdocs] Credentials source: {creds_source}")
                print(f"[gdocs] Credentials path: {creds_path or 'NONE'}")
            print(f"[gdocs] Token path: {self.token_path}")
            print(f"[gdocs] Scopes: {', '.join(SCOPES)}")

        # Check if we have valid credentials source
        if not creds_path and not env_creds_dict:
            print("\n❌ Error: No OAuth credentials found.")
            print("\nSearched locations:")
            print("  1. CLI flag: --oauth-client <path>")
            print("  2. Environment: $GOOGLE_OAUTH_CLIENT_JSON (path or inline JSON)")
            print("  3. Environment: $GOOGLE_OAUTH_CLIENT_ID + $GOOGLE_OAUTH_CLIENT_SECRET")

            xdg_base = os.environ.get("XDG_CONFIG_HOME")
            if xdg_base:
                print(f"  4. XDG config: {xdg_base}/juce-audio-service/google/oauth_client.json")
            else:
                print("  4. XDG config: (not set)")

            print(f"  5. Home config: {pathlib.Path.home()}/.config/juce-audio-service/google/oauth_client.json")

            repo_path = pathlib.Path(__file__).resolve().parent / "oauth_client.json"
            print(f"  6. Repo fallback: {repo_path}")

            print("\nTo fix this, choose one option:")
            print("  A. Place OAuth client JSON at: ~/.config/juce-audio-service/google/oauth_client.json")
            print("  B. Set: export GOOGLE_OAUTH_CLIENT_JSON='/path/to/oauth_client.json'")
            print("  C. Set: export GOOGLE_OAUTH_CLIENT_ID='...' and GOOGLE_OAUTH_CLIENT_SECRET='...'")
            print("\nSee README_docs_bridge.md for detailed setup instructions.")
            return False

        # Load existing token if available
        if pathlib.Path(self.token_path).exists():
            self.creds = Credentials.from_authorized_user_file(self.token_path, SCOPES)

        # Refresh or prompt for new credentials
        if not self.creds or not self.creds.valid:
            if self.creds and self.creds.expired and self.creds.refresh_token:
                try:
                    if verbose:
                        print("[gdocs] Refreshing expired token...")
                    self.creds.refresh(Request())
                except Exception as e:
                    if verbose:
                        print(f"[gdocs] Failed to refresh credentials: {e}")
                    self.creds = None

            if not self.creds:
                if verbose:
                    print("[gdocs] Starting OAuth consent flow...")

                try:
                    if env_creds_dict:
                        # Create flow from env vars
                        flow = InstalledAppFlow.from_client_config(
                            env_creds_dict,
                            SCOPES
                        )
                    else:
                        # Create flow from file
                        flow = InstalledAppFlow.from_client_secrets_file(
                            str(creds_path),
                            SCOPES
                        )

                    self.creds = flow.run_local_server(port=0)

                    if verbose:
                        print("[gdocs] OAuth consent completed successfully")

                except Exception as e:
                    print(f"\n❌ OAuth flow failed: {e}")
                    return False

            # Save credentials
            token_dir = pathlib.Path(self.token_path).parent
            token_dir.mkdir(parents=True, exist_ok=True)

            with open(self.token_path, 'w') as token:
                token.write(self.creds.to_json())

            os.chmod(self.token_path, 0o600)

            if verbose:
                print(f"[gdocs] Token saved to: {self.token_path}")

        self._build_services()
        return True

    def _build_services(self):
        """Build API service clients."""
        self.docs_service = build('docs', 'v1', credentials=self.creds)
        self.drive_service = build('drive', 'v3', credentials=self.creds)

    def get_doc_content(self, doc_id: str) -> Tuple[Optional[str], Optional[str], Optional[str]]:
        """
        Retrieve document content and revision ID.

        Args:
            doc_id: Google Doc ID

        Returns:
            Tuple of (content, revision_id, error_message)
            Returns (None, None, error) on failure
        """
        if not self.docs_service or not self.drive_service:
            return None, None, "Client not authenticated"

        try:
            # Get document content
            doc = self.docs_service.documents().get(documentId=doc_id).execute()

            # Extract text content
            content = self._extract_text(doc)

            # Get revision ID from Drive API
            file_metadata = self.drive_service.files().get(
                fileId=doc_id,
                fields='headRevisionId'
            ).execute()

            revision_id = file_metadata.get('headRevisionId', '')

            return content, revision_id, None

        except HttpError as e:
            error_msg = f"HTTP error {e.resp.status}: {e.error_details}"
            return None, None, error_msg
        except Exception as e:
            return None, None, f"Unexpected error: {e}"

    def _extract_text(self, doc: dict) -> str:
        """
        Extract plain text from document structure.

        Args:
            doc: Document JSON from API

        Returns:
            Plain text content
        """
        content_parts = []

        body = doc.get('body', {})
        content = body.get('content', [])

        for element in content:
            if 'paragraph' in element:
                paragraph = element['paragraph']
                paragraph_text = self._extract_paragraph_text(paragraph)
                if paragraph_text:
                    content_parts.append(paragraph_text)

            elif 'table' in element:
                # Handle tables (extract all text)
                table = element['table']
                for row in table.get('tableRows', []):
                    for cell in row.get('tableCells', []):
                        for cell_content in cell.get('content', []):
                            if 'paragraph' in cell_content:
                                para_text = self._extract_paragraph_text(cell_content['paragraph'])
                                if para_text:
                                    content_parts.append(para_text)

        return '\n'.join(content_parts)

    def _extract_paragraph_text(self, paragraph: dict) -> str:
        """Extract text from a paragraph element."""
        text_parts = []

        for element in paragraph.get('elements', []):
            text_run = element.get('textRun')
            if text_run:
                text_parts.append(text_run.get('content', ''))

        return ''.join(text_parts)

    def append_paragraph(self, doc_id: str, text: str) -> Optional[str]:
        """
        Append a paragraph to the end of the document.

        Args:
            doc_id: Google Doc ID
            text: Text to append

        Returns:
            Error message if failed, None on success
        """
        if not self.docs_service:
            return "Client not authenticated"

        try:
            # Get document to find end index
            doc = self.docs_service.documents().get(documentId=doc_id).execute()
            end_index = doc['body']['content'][-1]['endIndex'] - 1

            # Build insert request
            requests = [
                {
                    'insertText': {
                        'location': {'index': end_index},
                        'text': f'\n{text}\n'
                    }
                }
            ]

            self.docs_service.documents().batchUpdate(
                documentId=doc_id,
                body={'requests': requests}
            ).execute()

            return None

        except HttpError as e:
            return f"HTTP error {e.resp.status}: {e.error_details}"
        except Exception as e:
            return f"Unexpected error: {e}"

    def append_code_block(self, doc_id: str, language: str, content: str) -> Optional[str]:
        """
        Append a code block to the document.

        Args:
            doc_id: Google Doc ID
            language: Language identifier (e.g., "engineevents")
            content: Code block content

        Returns:
            Error message if failed, None on success
        """
        code_block = f'```{language}\n{content}\n```'
        return self.append_paragraph(doc_id, code_block)

    def append_heading(self, doc_id: str, text: str, level: int = 2) -> Optional[str]:
        """
        Append a heading to the end of the document.

        Args:
            doc_id: Google Doc ID
            text: Heading text
            level: Heading level (1-6), default 2

        Returns:
            Error message if failed, None on success
        """
        if not self.docs_service:
            return "Client not authenticated"

        if level < 1 or level > 6:
            return "Heading level must be between 1 and 6"

        try:
            # Get document to find end index
            doc = self.docs_service.documents().get(documentId=doc_id).execute()
            end_index = doc['body']['content'][-1]['endIndex'] - 1

            # Insert text and style as heading
            requests = [
                {
                    'insertText': {
                        'location': {'index': end_index},
                        'text': f'\n{text}\n'
                    }
                },
                {
                    'updateParagraphStyle': {
                        'range': {
                            'startIndex': end_index + 1,
                            'endIndex': end_index + 1 + len(text)
                        },
                        'paragraphStyle': {
                            'namedStyleType': f'HEADING_{level}'
                        },
                        'fields': 'namedStyleType'
                    }
                }
            ]

            self.docs_service.documents().batchUpdate(
                documentId=doc_id,
                body={'requests': requests}
            ).execute()

            return None

        except HttpError as e:
            return f"HTTP error {e.resp.status}: {e.error_details}"
        except Exception as e:
            return f"Unexpected error: {e}"