"""add wrapup-as-pause option to queues

Revision ID: 3fa2b1e7c9ab
Revises: abdc9ede147d
Create Date: 2025-08-17 12:00:00.000000

"""

# revision identifiers, used by Alembic.
revision = '3fa2b1e7c9ab'
down_revision = 'abdc9ede147d'
branch_labels = None
depends_on = None

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

AST_BOOL_NAME = 'ast_bool_values'
AST_BOOL_VALUES = [ '0', '1',
                    'off', 'on',
                    'false', 'true',
                    'no', 'yes' ]


def upgrade():
    """Add queues.wrapup_as_pause using the shared ast_bool_values enum.

    This mirrors the queues.conf option `wrapup-as-pause`.
    The application code reads it as a boolean-like value via ast_true().
    """
    # Define the named enum (PostgreSQL)
    ast_bool_values = ENUM(*AST_BOOL_VALUES, name=AST_BOOL_NAME, create_type=False)

    # Ensure the enum exists on PostgreSQL (safe if already created by prior migrations)
    if op.get_context().bind.dialect.name == 'postgresql':
        ast_bool_values.create(op.get_bind(), checkfirst=True)

    # Add the column (nullable, no server default; application provides default)
    op.add_column('queues', sa.Column('wrapup_as_pause', ast_bool_values))


def downgrade():
    # Drop the column
    op.drop_column('queues', 'wrapup_as_pause')
